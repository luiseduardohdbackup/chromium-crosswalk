// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "google_apis/drive/base_requests.h"

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/rand_util.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/task_runner_util.h"
#include "base/values.h"
#include "google_apis/drive/drive_api_parser.h"
#include "google_apis/drive/request_sender.h"
#include "google_apis/drive/request_util.h"
#include "google_apis/drive/task_util.h"
#include "google_apis/drive/time_util.h"
#include "net/base/elements_upload_data_stream.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/base/upload_data_stream.h"
#include "net/base/upload_element_reader.h"
#include "net/base/upload_file_element_reader.h"
#include "net/http/http_byte_range.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_status.h"

using net::URLFetcher;

namespace {

// Template for optional OAuth2 authorization HTTP header.
const char kAuthorizationHeaderFormat[] = "Authorization: Bearer %s";
// Template for GData API version HTTP header.
const char kGDataVersionHeader[] = "GData-Version: 3.0";

// Maximum number of attempts for re-authentication per request.
const int kMaxReAuthenticateAttemptsPerRequest = 1;

// Template for initiate upload of both GData WAPI and Drive API v2.
const char kUploadContentType[] = "X-Upload-Content-Type: ";
const char kUploadContentLength[] = "X-Upload-Content-Length: ";
const char kUploadResponseLocation[] = "location";

// Template for upload data range of both GData WAPI and Drive API v2.
const char kUploadContentRange[] = "Content-Range: bytes ";
const char kUploadResponseRange[] = "range";

// The prefix of multipart/related mime type.
const char kMultipartMimeTypePrefix[] = "multipart/related; boundary=";

// Template for multipart request body.
const char kMessageFormatBeforeFile[] =
    "--%s\nContent-Type: %s\n\n%s\n--%s\nContent-Type: %s\n\n";
const char kMessageFormatAfterFile[] = "\n--%s--";

// Characters to be used for multipart/related boundary.
const char kBoundaryCharacters[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
// Size of multipart/related's boundary.
const char kBoundarySize = 70;

// Parses JSON passed in |json| on |blocking_task_runner|. Runs |callback| on
// the calling thread when finished with either success or failure.
// The callback must not be null.
void ParseJsonOnBlockingPool(
    base::TaskRunner* blocking_task_runner,
    const std::string& json,
    const base::Callback<void(scoped_ptr<base::Value> value)>& callback) {
  base::PostTaskAndReplyWithResult(
      blocking_task_runner,
      FROM_HERE,
      base::Bind(&google_apis::ParseJson, json),
      callback);
}

// Returns response headers as a string. Returns a warning message if
// |url_fetcher| does not contain a valid response. Used only for debugging.
std::string GetResponseHeadersAsString(const URLFetcher* url_fetcher) {
  // net::HttpResponseHeaders::raw_headers(), as the name implies, stores
  // all headers in their raw format, i.e each header is null-terminated.
  // So logging raw_headers() only shows the first header, which is probably
  // the status line.  GetNormalizedHeaders, on the other hand, will show all
  // the headers, one per line, which is probably what we want.
  std::string headers;
  // Check that response code indicates response headers are valid (i.e. not
  // malformed) before we retrieve the headers.
  if (url_fetcher->GetResponseCode() == URLFetcher::RESPONSE_CODE_INVALID) {
    headers.assign("Response headers are malformed!!");
  } else {
    url_fetcher->GetResponseHeaders()->GetNormalizedHeaders(&headers);
  }
  return headers;
}

bool IsSuccessfulResponseCode(int response_code) {
  return 200 <= response_code && response_code <= 299;
}

// Creates metadata JSON string for multipart uploading.
// All the values are optional. If the value is empty or null, the value does
// not appear in the metadata.
std::string CreateMultipartUploadMetadataJson(
    const std::string& title,
    const std::string& parent_resource_id,
    const base::Time& modified_date,
    const base::Time& last_viewed_by_me_date) {
  base::DictionaryValue root;
  if (!title.empty())
    root.SetString("title", title);

  // Fill parent link.
  if (!parent_resource_id.empty()) {
    scoped_ptr<base::ListValue> parents(new base::ListValue);
    parents->Append(
        google_apis::util::CreateParentValue(parent_resource_id).release());
    root.Set("parents", parents.release());
  }

  if (!modified_date.is_null())
    root.SetString("modifiedDate",
                   google_apis::util::FormatTimeAsString(modified_date));

  if (!last_viewed_by_me_date.is_null()) {
    root.SetString("lastViewedByMeDate", google_apis::util::FormatTimeAsString(
                                             last_viewed_by_me_date));
  }

  std::string json_string;
  base::JSONWriter::Write(&root, &json_string);
  return json_string;
}

// Obtains the multipart body for the metadata string and file contents. If
// predetermined_boundary is empty, the function generates the boundary string.
bool GetMultipartContent(const std::string& predetermined_boundary,
                         const std::string& metadata_json,
                         const std::string& content_type,
                         const base::FilePath& path,
                         std::string* upload_content_type,
                         std::string* upload_content_data) {
  std::string file_content;
  if (!ReadFileToString(path, &file_content))
    return false;

  std::string boundary;
  if (predetermined_boundary.empty()) {
    while (true) {
      boundary.resize(kBoundarySize);
      for (int i = 0; i < kBoundarySize; ++i) {
        // Subtract 2 from the array size to exclude '\0', and to turn the size
        // into the last index.
        const int last_char_index = arraysize(kBoundaryCharacters) - 2;
        boundary[i] = kBoundaryCharacters[base::RandInt(0,  last_char_index)];
      }
      if (metadata_json.find(boundary, 0) == std::string::npos &&
          file_content.find(boundary, 0) == std::string::npos) {
        break;
      }
    }
  } else {
    boundary = predetermined_boundary;
  }
  const std::string body_before_file = base::StringPrintf(
      kMessageFormatBeforeFile, boundary.c_str(), "application/json",
      metadata_json.c_str(), boundary.c_str(), content_type.c_str());
  const std::string body_after_file =
      base::StringPrintf(kMessageFormatAfterFile, boundary.c_str());

  *upload_content_type = kMultipartMimeTypePrefix + boundary;
  *upload_content_data = body_before_file + file_content + body_after_file;
  return true;
}

}  // namespace

namespace google_apis {

scoped_ptr<base::Value> ParseJson(const std::string& json) {
  int error_code = -1;
  std::string error_message;
  scoped_ptr<base::Value> value(base::JSONReader::ReadAndReturnError(
      json, base::JSON_PARSE_RFC, &error_code, &error_message));

  if (!value.get()) {
    std::string trimmed_json;
    if (json.size() < 80) {
      trimmed_json  = json;
    } else {
      // Take the first 50 and the last 10 bytes.
      trimmed_json = base::StringPrintf(
          "%s [%s bytes] %s",
          json.substr(0, 50).c_str(),
          base::Uint64ToString(json.size() - 60).c_str(),
          json.substr(json.size() - 10).c_str());
    }
    LOG(WARNING) << "Error while parsing entry response: " << error_message
                 << ", code: " << error_code << ", json:\n" << trimmed_json;
  }
  return value.Pass();
}

//=========================== ResponseWriter ==================================
ResponseWriter::ResponseWriter(base::SequencedTaskRunner* file_task_runner,
                               const base::FilePath& file_path,
                               const GetContentCallback& get_content_callback)
    : get_content_callback_(get_content_callback),
      weak_ptr_factory_(this) {
  if (!file_path.empty()) {
    file_writer_.reset(
        new net::URLFetcherFileWriter(file_task_runner, file_path));
  }
}

ResponseWriter::~ResponseWriter() {
}

void ResponseWriter::DisownFile() {
  DCHECK(file_writer_);
  file_writer_->DisownFile();
}

int ResponseWriter::Initialize(const net::CompletionCallback& callback) {
  if (file_writer_)
    return file_writer_->Initialize(callback);

  data_.clear();
  return net::OK;
}

int ResponseWriter::Write(net::IOBuffer* buffer,
                          int num_bytes,
                          const net::CompletionCallback& callback) {
  if (!get_content_callback_.is_null()) {
    get_content_callback_.Run(
        HTTP_SUCCESS,
        make_scoped_ptr(new std::string(buffer->data(), num_bytes)));
  }

  if (file_writer_) {
    const int result = file_writer_->Write(
        buffer, num_bytes,
        base::Bind(&ResponseWriter::DidWrite,
                   weak_ptr_factory_.GetWeakPtr(),
                   make_scoped_refptr(buffer), callback));
    if (result != net::ERR_IO_PENDING)
      DidWrite(buffer, net::CompletionCallback(), result);
    return result;
  }

  data_.append(buffer->data(), num_bytes);
  return num_bytes;
}

int ResponseWriter::Finish(const net::CompletionCallback& callback) {
  if (file_writer_)
    return file_writer_->Finish(callback);

  return net::OK;
}

void ResponseWriter::DidWrite(scoped_refptr<net::IOBuffer> buffer,
                              const net::CompletionCallback& callback,
                              int result) {
  if (result > 0) {
    // Even if file_writer_ is used, append the data to |data_|, so that it can
    // be used to get error information in case of server side errors.
    // The size limit is to avoid consuming too much redundant memory.
    const size_t kMaxStringSize = 1024*1024;
    if (data_.size() < kMaxStringSize) {
      data_.append(buffer->data(), std::min(static_cast<size_t>(result),
                                            kMaxStringSize - data_.size()));
    }
  }

  if (!callback.is_null())
    callback.Run(result);
}

//============================ UrlFetchRequestBase ===========================

UrlFetchRequestBase::UrlFetchRequestBase(RequestSender* sender)
    : re_authenticate_count_(0),
      response_writer_(NULL),
      sender_(sender),
      error_code_(GDATA_OTHER_ERROR),
      weak_ptr_factory_(this) {
}

UrlFetchRequestBase::~UrlFetchRequestBase() {}

void UrlFetchRequestBase::Start(const std::string& access_token,
                                const std::string& custom_user_agent,
                                const ReAuthenticateCallback& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!access_token.empty());
  DCHECK(!callback.is_null());
  DCHECK(re_authenticate_callback_.is_null());

  re_authenticate_callback_ = callback;

  GURL url = GetURL();
  if (url.is_empty()) {
    // Error is found on generating the url. Send the error message to the
    // callback, and then return immediately without trying to connect
    // to the server.
    RunCallbackOnPrematureFailure(GDATA_OTHER_ERROR);
    return;
  }
  DVLOG(1) << "URL: " << url.spec();

  URLFetcher::RequestType request_type = GetRequestType();
  url_fetcher_.reset(URLFetcher::Create(url, request_type, this));
  url_fetcher_->SetRequestContext(sender_->url_request_context_getter());
  // Always set flags to neither send nor save cookies.
  url_fetcher_->SetLoadFlags(
      net::LOAD_DO_NOT_SEND_COOKIES | net::LOAD_DO_NOT_SAVE_COOKIES |
      net::LOAD_DISABLE_CACHE);

  base::FilePath output_file_path;
  GetContentCallback get_content_callback;
  GetOutputFilePath(&output_file_path, &get_content_callback);
  if (!get_content_callback.is_null())
    get_content_callback = CreateRelayCallback(get_content_callback);
  response_writer_ = new ResponseWriter(blocking_task_runner(),
                                        output_file_path,
                                        get_content_callback);
  url_fetcher_->SaveResponseWithWriter(
      scoped_ptr<net::URLFetcherResponseWriter>(response_writer_));

  // Add request headers.
  // Note that SetExtraRequestHeaders clears the current headers and sets it
  // to the passed-in headers, so calling it for each header will result in
  // only the last header being set in request headers.
  if (!custom_user_agent.empty())
    url_fetcher_->AddExtraRequestHeader("User-Agent: " + custom_user_agent);
  url_fetcher_->AddExtraRequestHeader(kGDataVersionHeader);
  url_fetcher_->AddExtraRequestHeader(
      base::StringPrintf(kAuthorizationHeaderFormat, access_token.data()));
  std::vector<std::string> headers = GetExtraRequestHeaders();
  for (size_t i = 0; i < headers.size(); ++i) {
    url_fetcher_->AddExtraRequestHeader(headers[i]);
    DVLOG(1) << "Extra header: " << headers[i];
  }

  // Set upload data if available.
  std::string upload_content_type;
  std::string upload_content;
  if (GetContentData(&upload_content_type, &upload_content)) {
    url_fetcher_->SetUploadData(upload_content_type, upload_content);
  } else {
    base::FilePath local_file_path;
    int64 range_offset = 0;
    int64 range_length = 0;
    if (GetContentFile(&local_file_path, &range_offset, &range_length,
                       &upload_content_type)) {
      url_fetcher_->SetUploadFilePath(
          upload_content_type,
          local_file_path,
          range_offset,
          range_length,
          blocking_task_runner());
    } else {
      // Even if there is no content data, UrlFetcher requires to set empty
      // upload data string for POST, PUT and PATCH methods, explicitly.
      // It is because that most requests of those methods have non-empty
      // body, and UrlFetcher checks whether it is actually not forgotten.
      if (request_type == URLFetcher::POST ||
          request_type == URLFetcher::PUT ||
          request_type == URLFetcher::PATCH) {
        // Set empty upload content-type and upload content, so that
        // the request will have no "Content-type: " header and no content.
        url_fetcher_->SetUploadData(std::string(), std::string());
      }
    }
  }

  url_fetcher_->Start();
}

URLFetcher::RequestType UrlFetchRequestBase::GetRequestType() const {
  return URLFetcher::GET;
}

std::vector<std::string> UrlFetchRequestBase::GetExtraRequestHeaders() const {
  return std::vector<std::string>();
}

bool UrlFetchRequestBase::GetContentData(std::string* upload_content_type,
                                         std::string* upload_content) {
  return false;
}

bool UrlFetchRequestBase::GetContentFile(base::FilePath* local_file_path,
                                         int64* range_offset,
                                         int64* range_length,
                                         std::string* upload_content_type) {
  return false;
}

void UrlFetchRequestBase::GetOutputFilePath(
    base::FilePath* local_file_path,
    GetContentCallback* get_content_callback) {
}

void UrlFetchRequestBase::Cancel() {
  response_writer_ = NULL;
  url_fetcher_.reset(NULL);
  RunCallbackOnPrematureFailure(GDATA_CANCELLED);
  sender_->RequestFinished(this);
}

GDataErrorCode UrlFetchRequestBase::GetErrorCode() {
  return error_code_;
}

bool UrlFetchRequestBase::CalledOnValidThread() {
  return thread_checker_.CalledOnValidThread();
}

base::SequencedTaskRunner* UrlFetchRequestBase::blocking_task_runner() const {
  return sender_->blocking_task_runner();
}

void UrlFetchRequestBase::OnProcessURLFetchResultsComplete() {
  sender_->RequestFinished(this);
}

void UrlFetchRequestBase::OnURLFetchComplete(const URLFetcher* source) {
  DVLOG(1) << "Response headers:\n" << GetResponseHeadersAsString(source);

  // Determine error code.
  error_code_ = static_cast<GDataErrorCode>(source->GetResponseCode());
  if (!source->GetStatus().is_success()) {
    switch (source->GetStatus().error()) {
      case net::ERR_NETWORK_CHANGED:
        error_code_ = GDATA_NO_CONNECTION;
        break;
      default:
        error_code_ = GDATA_OTHER_ERROR;
    }
  }

  // The server may return detailed error status in JSON.
  // See https://developers.google.com/drive/handle-errors
  if (!IsSuccessfulResponseCode(error_code_)) {
    DVLOG(1) << response_writer_->data();

    const char kErrorKey[] = "error";
    const char kErrorErrorsKey[] = "errors";
    const char kErrorReasonKey[] = "reason";
    const char kErrorMessageKey[] = "message";
    const char kErrorReasonRateLimitExceeded[] = "rateLimitExceeded";
    const char kErrorReasonUserRateLimitExceeded[] = "userRateLimitExceeded";
    const char kErrorReasonQuotaExceeded[] = "quotaExceeded";

    scoped_ptr<base::Value> value(ParseJson(response_writer_->data()));
    base::DictionaryValue* dictionary = NULL;
    base::DictionaryValue* error = NULL;
    if (value &&
        value->GetAsDictionary(&dictionary) &&
        dictionary->GetDictionaryWithoutPathExpansion(kErrorKey, &error)) {
      // Get error message.
      std::string message;
      error->GetStringWithoutPathExpansion(kErrorMessageKey, &message);
      DLOG(ERROR) << "code: " << error_code_ << ", message: " << message;

      // Override the error code based on the reason of the first error.
      base::ListValue* errors = NULL;
      base::DictionaryValue* first_error = NULL;
      if (error->GetListWithoutPathExpansion(kErrorErrorsKey, &errors) &&
          errors->GetDictionary(0, &first_error)) {
        std::string reason;
        first_error->GetStringWithoutPathExpansion(kErrorReasonKey, &reason);
        if (reason == kErrorReasonRateLimitExceeded ||
            reason == kErrorReasonUserRateLimitExceeded)
          error_code_ = HTTP_SERVICE_UNAVAILABLE;
        if (reason == kErrorReasonQuotaExceeded)
          error_code_ = GDATA_NO_SPACE;
      }
    }
  }

  // Handle authentication failure.
  if (error_code_ == HTTP_UNAUTHORIZED) {
    if (++re_authenticate_count_ <= kMaxReAuthenticateAttemptsPerRequest) {
      // Reset re_authenticate_callback_ so Start() can be called again.
      ReAuthenticateCallback callback = re_authenticate_callback_;
      re_authenticate_callback_.Reset();
      callback.Run(this);
      return;
    }

    OnAuthFailed(error_code_);
    return;
  }

  // Overridden by each specialization
  ProcessURLFetchResults(source);
}

void UrlFetchRequestBase::OnAuthFailed(GDataErrorCode code) {
  RunCallbackOnPrematureFailure(code);
  sender_->RequestFinished(this);
}

base::WeakPtr<AuthenticatedRequestInterface>
UrlFetchRequestBase::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

//============================ EntryActionRequest ============================

EntryActionRequest::EntryActionRequest(RequestSender* sender,
                                       const EntryActionCallback& callback)
    : UrlFetchRequestBase(sender),
      callback_(callback) {
  DCHECK(!callback_.is_null());
}

EntryActionRequest::~EntryActionRequest() {}

void EntryActionRequest::ProcessURLFetchResults(const URLFetcher* source) {
  callback_.Run(GetErrorCode());
  OnProcessURLFetchResultsComplete();
}

void EntryActionRequest::RunCallbackOnPrematureFailure(GDataErrorCode code) {
  callback_.Run(code);
}

//========================= InitiateUploadRequestBase ========================

InitiateUploadRequestBase::InitiateUploadRequestBase(
    RequestSender* sender,
    const InitiateUploadCallback& callback,
    const std::string& content_type,
    int64 content_length)
    : UrlFetchRequestBase(sender),
      callback_(callback),
      content_type_(content_type),
      content_length_(content_length) {
  DCHECK(!callback_.is_null());
  DCHECK(!content_type_.empty());
  DCHECK_GE(content_length_, 0);
}

InitiateUploadRequestBase::~InitiateUploadRequestBase() {}

void InitiateUploadRequestBase::ProcessURLFetchResults(
    const URLFetcher* source) {
  GDataErrorCode code = GetErrorCode();

  std::string upload_location;
  if (code == HTTP_SUCCESS) {
    // Retrieve value of the first "Location" header.
    source->GetResponseHeaders()->EnumerateHeader(NULL,
                                                  kUploadResponseLocation,
                                                  &upload_location);
  }

  callback_.Run(code, GURL(upload_location));
  OnProcessURLFetchResultsComplete();
}

void InitiateUploadRequestBase::RunCallbackOnPrematureFailure(
    GDataErrorCode code) {
  callback_.Run(code, GURL());
}

std::vector<std::string>
InitiateUploadRequestBase::GetExtraRequestHeaders() const {
  std::vector<std::string> headers;
  headers.push_back(kUploadContentType + content_type_);
  headers.push_back(
      kUploadContentLength + base::Int64ToString(content_length_));
  return headers;
}

//============================ UploadRangeResponse =============================

UploadRangeResponse::UploadRangeResponse()
    : code(HTTP_SUCCESS),
      start_position_received(0),
      end_position_received(0) {
}

UploadRangeResponse::UploadRangeResponse(GDataErrorCode code,
                                         int64 start_position_received,
                                         int64 end_position_received)
    : code(code),
      start_position_received(start_position_received),
      end_position_received(end_position_received) {
}

UploadRangeResponse::~UploadRangeResponse() {
}

//========================== UploadRangeRequestBase ==========================

UploadRangeRequestBase::UploadRangeRequestBase(RequestSender* sender,
                                               const GURL& upload_url)
    : UrlFetchRequestBase(sender),
      upload_url_(upload_url),
      weak_ptr_factory_(this) {
}

UploadRangeRequestBase::~UploadRangeRequestBase() {}

GURL UploadRangeRequestBase::GetURL() const {
  // This is very tricky to get json from this request. To do that, &alt=json
  // has to be appended not here but in InitiateUploadRequestBase::GetURL().
  return upload_url_;
}

URLFetcher::RequestType UploadRangeRequestBase::GetRequestType() const {
  return URLFetcher::PUT;
}

void UploadRangeRequestBase::ProcessURLFetchResults(
    const URLFetcher* source) {
  GDataErrorCode code = GetErrorCode();
  net::HttpResponseHeaders* hdrs = source->GetResponseHeaders();

  if (code == HTTP_RESUME_INCOMPLETE) {
    // Retrieve value of the first "Range" header.
    // The Range header is appeared only if there is at least one received
    // byte. So, initialize the positions by 0 so that the [0,0) will be
    // returned via the |callback_| for empty data case.
    int64 start_position_received = 0;
    int64 end_position_received = 0;
    std::string range_received;
    hdrs->EnumerateHeader(NULL, kUploadResponseRange, &range_received);
    if (!range_received.empty()) {  // Parse the range header.
      std::vector<net::HttpByteRange> ranges;
      if (net::HttpUtil::ParseRangeHeader(range_received, &ranges) &&
          !ranges.empty() ) {
        // We only care about the first start-end pair in the range.
        //
        // Range header represents the range inclusively, while we are treating
        // ranges exclusively (i.e., end_position_received should be one passed
        // the last valid index). So "+ 1" is added.
        start_position_received = ranges[0].first_byte_position();
        end_position_received = ranges[0].last_byte_position() + 1;
      }
    }
    // The Range header has the received data range, so the start position
    // should be always 0.
    DCHECK_EQ(start_position_received, 0);

    OnRangeRequestComplete(UploadRangeResponse(code,
                                               start_position_received,
                                               end_position_received),
                           scoped_ptr<base::Value>());

    OnProcessURLFetchResultsComplete();
  } else if (code == HTTP_CREATED || code == HTTP_SUCCESS) {
    // The upload is successfully done. Parse the response which should be
    // the entry's metadata.
    ParseJsonOnBlockingPool(blocking_task_runner(),
                            response_writer()->data(),
                            base::Bind(&UploadRangeRequestBase::OnDataParsed,
                                       weak_ptr_factory_.GetWeakPtr(),
                                       code));
  } else {
    // Failed to upload. Run callbacks to notify the error.
    OnRangeRequestComplete(
        UploadRangeResponse(code, -1, -1), scoped_ptr<base::Value>());
    OnProcessURLFetchResultsComplete();
  }
}

void UploadRangeRequestBase::OnDataParsed(GDataErrorCode code,
                                          scoped_ptr<base::Value> value) {
  DCHECK(CalledOnValidThread());
  DCHECK(code == HTTP_CREATED || code == HTTP_SUCCESS);

  OnRangeRequestComplete(UploadRangeResponse(code, -1, -1), value.Pass());
  OnProcessURLFetchResultsComplete();
}

void UploadRangeRequestBase::RunCallbackOnPrematureFailure(
    GDataErrorCode code) {
  OnRangeRequestComplete(
      UploadRangeResponse(code, 0, 0), scoped_ptr<base::Value>());
}

//========================== ResumeUploadRequestBase =========================

ResumeUploadRequestBase::ResumeUploadRequestBase(
    RequestSender* sender,
    const GURL& upload_location,
    int64 start_position,
    int64 end_position,
    int64 content_length,
    const std::string& content_type,
    const base::FilePath& local_file_path)
    : UploadRangeRequestBase(sender, upload_location),
      start_position_(start_position),
      end_position_(end_position),
      content_length_(content_length),
      content_type_(content_type),
      local_file_path_(local_file_path) {
  DCHECK_LE(start_position_, end_position_);
}

ResumeUploadRequestBase::~ResumeUploadRequestBase() {}

std::vector<std::string>
ResumeUploadRequestBase::GetExtraRequestHeaders() const {
  if (content_length_ == 0) {
    // For uploading an empty document, just PUT an empty content.
    DCHECK_EQ(start_position_, 0);
    DCHECK_EQ(end_position_, 0);
    return std::vector<std::string>();
  }

  // The header looks like
  // Content-Range: bytes <start_position>-<end_position>/<content_length>
  // for example:
  // Content-Range: bytes 7864320-8388607/13851821
  // The header takes inclusive range, so we adjust by "end_position - 1".
  DCHECK_GE(start_position_, 0);
  DCHECK_GT(end_position_, 0);
  DCHECK_GE(content_length_, 0);

  std::vector<std::string> headers;
  headers.push_back(
      std::string(kUploadContentRange) +
      base::Int64ToString(start_position_) + "-" +
      base::Int64ToString(end_position_ - 1) + "/" +
      base::Int64ToString(content_length_));
  return headers;
}

bool ResumeUploadRequestBase::GetContentFile(
    base::FilePath* local_file_path,
    int64* range_offset,
    int64* range_length,
    std::string* upload_content_type) {
  if (start_position_ == end_position_) {
    // No content data.
    return false;
  }

  *local_file_path = local_file_path_;
  *range_offset = start_position_;
  *range_length = end_position_ - start_position_;
  *upload_content_type = content_type_;
  return true;
}

//======================== GetUploadStatusRequestBase ========================

GetUploadStatusRequestBase::GetUploadStatusRequestBase(RequestSender* sender,
                                                       const GURL& upload_url,
                                                       int64 content_length)
    : UploadRangeRequestBase(sender, upload_url),
      content_length_(content_length) {}

GetUploadStatusRequestBase::~GetUploadStatusRequestBase() {}

std::vector<std::string>
GetUploadStatusRequestBase::GetExtraRequestHeaders() const {
  // The header looks like
  // Content-Range: bytes */<content_length>
  // for example:
  // Content-Range: bytes */13851821
  DCHECK_GE(content_length_, 0);

  std::vector<std::string> headers;
  headers.push_back(
      std::string(kUploadContentRange) + "*/" +
      base::Int64ToString(content_length_));
  return headers;
}

//========================= MultipartUploadRequestBase ========================

MultipartUploadRequestBase::MultipartUploadRequestBase(
    RequestSender* sender,
    const std::string& title,
    const std::string& parent_resource_id,
    const std::string& content_type,
    int64 content_length,
    const base::Time& modified_date,
    const base::Time& last_viewed_by_me_date,
    const base::FilePath& local_file_path,
    const FileResourceCallback& callback,
    const ProgressCallback& progress_callback)
    : UrlFetchRequestBase(sender),
      metadata_json_(CreateMultipartUploadMetadataJson(title,
                                                       parent_resource_id,
                                                       modified_date,
                                                       last_viewed_by_me_date)),
      content_type_(content_type),
      local_path_(local_file_path),
      has_modified_date_(!modified_date.is_null()),
      callback_(callback),
      progress_callback_(progress_callback),
      weak_ptr_factory_(this) {
  DCHECK(!content_type.empty());
  DCHECK_GE(content_length, 0);
  DCHECK(!local_file_path.empty());
  DCHECK(!callback.is_null());
}

MultipartUploadRequestBase::~MultipartUploadRequestBase() {
}

void MultipartUploadRequestBase::Start(const std::string& access_token,
                                       const std::string& custom_user_agent,
                                       const ReAuthenticateCallback& callback) {
  // If the request is cancelled, the request instance will be deleted in
  // |UrlFetchRequestBase::Cancel| and OnPrepareUploadContent won't be called.
  std::string* const upload_content_type = new std::string();
  std::string* const upload_content_data = new std::string();
  PostTaskAndReplyWithResult(
      blocking_task_runner(), FROM_HERE,
      base::Bind(&GetMultipartContent, boundary_, metadata_json_, content_type_,
                 local_path_, base::Unretained(upload_content_type),
                 base::Unretained(upload_content_data)),
      base::Bind(&MultipartUploadRequestBase::OnPrepareUploadContent,
                 weak_ptr_factory_.GetWeakPtr(), access_token,
                 custom_user_agent, callback, base::Owned(upload_content_type),
                 base::Owned(upload_content_data)));
}

void MultipartUploadRequestBase::OnPrepareUploadContent(
    const std::string& access_token,
    const std::string& custom_user_agent,
    const ReAuthenticateCallback& callback,
    std::string* upload_content_type,
    std::string* upload_content_data,
    bool result) {
  if (!result) {
    RunCallbackOnPrematureFailure(GDATA_FILE_ERROR);
    return;
  }
  upload_content_type_.swap(*upload_content_type);
  upload_content_data_.swap(*upload_content_data);
  UrlFetchRequestBase::Start(access_token, custom_user_agent, callback);
}

void MultipartUploadRequestBase::SetBoundaryForTesting(
    const std::string& boundary) {
  boundary_ = boundary;
}

bool MultipartUploadRequestBase::GetContentData(
    std::string* upload_content_type,
    std::string* upload_content_data) {
  // TODO(hirono): Pass stream instead of actual data to reduce memory usage.
  upload_content_type->swap(upload_content_type_);
  upload_content_data->swap(upload_content_data_);
  return true;
}

void MultipartUploadRequestBase::ProcessURLFetchResults(
    const URLFetcher* source) {
  // The upload is successfully done. Parse the response which should be
  // the entry's metadata.
  const GDataErrorCode code = GetErrorCode();
  if (code == HTTP_CREATED || code == HTTP_SUCCESS) {
    ParseJsonOnBlockingPool(
        blocking_task_runner(), response_writer()->data(),
        base::Bind(&MultipartUploadRequestBase::OnDataParsed,
                   weak_ptr_factory_.GetWeakPtr(), code));
  } else {
    OnDataParsed(code, scoped_ptr<base::Value>());
  }
}

void MultipartUploadRequestBase::RunCallbackOnPrematureFailure(
    GDataErrorCode code) {
  callback_.Run(code, scoped_ptr<FileResource>());
}

void MultipartUploadRequestBase::OnURLFetchUploadProgress(
    const net::URLFetcher* source,
    int64 current,
    int64 total) {
  if (!progress_callback_.is_null())
    progress_callback_.Run(current, total);
}

void MultipartUploadRequestBase::OnDataParsed(GDataErrorCode code,
                                              scoped_ptr<base::Value> value) {
  DCHECK(CalledOnValidThread());
  if (value)
    callback_.Run(code, google_apis::FileResource::CreateFrom(*value));
  else
    callback_.Run(GDATA_PARSE_ERROR, scoped_ptr<FileResource>());
  OnProcessURLFetchResultsComplete();
}

//============================ DownloadFileRequestBase =========================

DownloadFileRequestBase::DownloadFileRequestBase(
    RequestSender* sender,
    const DownloadActionCallback& download_action_callback,
    const GetContentCallback& get_content_callback,
    const ProgressCallback& progress_callback,
    const GURL& download_url,
    const base::FilePath& output_file_path)
    : UrlFetchRequestBase(sender),
      download_action_callback_(download_action_callback),
      get_content_callback_(get_content_callback),
      progress_callback_(progress_callback),
      download_url_(download_url),
      output_file_path_(output_file_path) {
  DCHECK(!download_action_callback_.is_null());
  DCHECK(!output_file_path_.empty());
  // get_content_callback may be null.
}

DownloadFileRequestBase::~DownloadFileRequestBase() {}

// Overridden from UrlFetchRequestBase.
GURL DownloadFileRequestBase::GetURL() const {
  return download_url_;
}

void DownloadFileRequestBase::GetOutputFilePath(
    base::FilePath* local_file_path,
    GetContentCallback* get_content_callback) {
  // Configure so that the downloaded content is saved to |output_file_path_|.
  *local_file_path = output_file_path_;
  *get_content_callback = get_content_callback_;
}

void DownloadFileRequestBase::OnURLFetchDownloadProgress(
    const URLFetcher* source,
    int64 current,
    int64 total) {
  if (!progress_callback_.is_null())
    progress_callback_.Run(current, total);
}

void DownloadFileRequestBase::ProcessURLFetchResults(const URLFetcher* source) {
  GDataErrorCode code = GetErrorCode();

  // Take over the ownership of the the downloaded temp file.
  base::FilePath temp_file;
  if (code == HTTP_SUCCESS) {
    response_writer()->DisownFile();
    temp_file = output_file_path_;
  }

  download_action_callback_.Run(code, temp_file);
  OnProcessURLFetchResultsComplete();
}

void DownloadFileRequestBase::RunCallbackOnPrematureFailure(
    GDataErrorCode code) {
  download_action_callback_.Run(code, base::FilePath());
}

}  // namespace google_apis
