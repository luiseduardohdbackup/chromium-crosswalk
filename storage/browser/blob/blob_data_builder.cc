// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <storage/browser/blob/blob_data_builder.h>
#include "base/time/time.h"

namespace storage {

BlobDataBuilder::BlobDataBuilder(const std::string& uuid) : uuid_(uuid) {
}
BlobDataBuilder::~BlobDataBuilder() {
}

void BlobDataBuilder::AppendData(const char* data, size_t length) {
  DCHECK(length > 0);
  scoped_ptr<DataElement> element(new DataElement());
  element->SetToBytes(data, length);
  items_.push_back(new BlobDataItem(element.Pass()));
}

void BlobDataBuilder::AppendFile(const base::FilePath& file_path,
                                 uint64 offset,
                                 uint64 length,
                                 const base::Time& expected_modification_time) {
  DCHECK(length > 0);
  scoped_ptr<DataElement> element(new DataElement());
  element->SetToFilePathRange(file_path, offset, length,
                              expected_modification_time);
  items_.push_back(new BlobDataItem(element.Pass()));
}

void BlobDataBuilder::AppendFile(
    const base::FilePath& file_path,
    uint64 offset,
    uint64 length,
    const base::Time& expected_modification_time,
    scoped_refptr<ShareableFileReference> shareable_file) {
  DCHECK(length > 0);
  scoped_ptr<DataElement> element(new DataElement());
  element->SetToFilePathRange(file_path, offset, length,
                              expected_modification_time);
  items_.push_back(new BlobDataItem(element.Pass(), shareable_file));
}

void BlobDataBuilder::AppendBlob(const std::string& uuid,
                                 uint64 offset,
                                 uint64 length) {
  DCHECK_GT(length, 0ul);
  scoped_ptr<DataElement> element(new DataElement());
  element->SetToBlobRange(uuid, offset, length);
  items_.push_back(new BlobDataItem(element.Pass()));
}

void BlobDataBuilder::AppendFileSystemFile(
    const GURL& url,
    uint64 offset,
    uint64 length,
    const base::Time& expected_modification_time) {
  DCHECK(length > 0);
  scoped_ptr<DataElement> element(new DataElement());
  element->SetToFileSystemUrlRange(url, offset, length,
                                   expected_modification_time);
  items_.push_back(new BlobDataItem(element.Pass()));
}

size_t BlobDataBuilder::GetMemoryUsage() const {
  int64 memory = 0;
  for (const auto& data_item : items_) {
    if (data_item->type() == DataElement::TYPE_BYTES)
      memory += data_item->length();
  }
  return memory;
}

scoped_ptr<BlobDataSnapshot> BlobDataBuilder::BuildSnapshot() {
  return scoped_ptr<BlobDataSnapshot>(new BlobDataSnapshot(uuid_, content_type_,
                                                           content_disposition_,
                                                           items_)).Pass();
}

}  // namespace storage
