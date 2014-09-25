// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_SHELL_BROWSER_URL_REQUEST_CONTEXT_FACTORY_H_
#define CHROMECAST_SHELL_BROWSER_URL_REQUEST_CONTEXT_FACTORY_H_

#include "content/public/browser/content_browser_client.h"
#include "net/http/http_network_session.h"

namespace net {
class HttpTransactionFactory;
class HttpUserAgentSettings;
class ProxyConfigService;
class URLRequestJobFactory;
}  // namespace net

namespace chromecast {
namespace shell {

class URLRequestContextFactory {
 public:
  URLRequestContextFactory();
  ~URLRequestContextFactory();

  // Some members must be initialized on UI thread.
  void InitializeOnUIThread();

  // Since main context requires a bunch of input params, if these get called
  // multiple times, either multiple main contexts should be supported/managed
  // or the input params need to be the same as before.  So to be safe,
  // the CreateMainGetter function currently DCHECK to make sure it is not
  // called more than once.
  // The media and system getters however, do not need input, so it is actually
  // safe to call these multiple times.  The impl create only 1 getter of each
  // type and return the same instance each time the methods are called, thus
  // the name difference.
  net::URLRequestContextGetter* GetSystemGetter();
  net::URLRequestContextGetter* CreateMainGetter(
      content::BrowserContext* browser_context,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors);
  net::URLRequestContextGetter* GetMainGetter();
  net::URLRequestContextGetter* GetMediaGetter();

 private:
  class URLRequestContextGetter;
  class MainURLRequestContextGetter;
  friend class URLRequestContextGetter;
  friend class MainURLRequestContextGetter;

  void InitializeSystemContextDependencies();
  void InitializeMainContextDependencies(
      net::HttpTransactionFactory* factory,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors);
  void InitializeMediaContextDependencies(net::HttpTransactionFactory* factory);

  void PopulateNetworkSessionParams(bool ignore_certificate_errors,
                                    net::HttpNetworkSession::Params* params);

  // These are called by the RequestContextGetters to create each
  // RequestContext.
  // They must be called on the IO thread.
  net::URLRequestContext* CreateSystemRequestContext();
  net::URLRequestContext* CreateMediaRequestContext();
  net::URLRequestContext* CreateMainRequestContext(
      content::BrowserContext* browser_context,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors);

  scoped_refptr<net::URLRequestContextGetter> system_getter_;
  scoped_refptr<net::URLRequestContextGetter> media_getter_;
  scoped_refptr<net::URLRequestContextGetter> main_getter_;

  // Shared objects for all contexts.
  // The URLRequestContextStorage class is not used as owner to these objects
  // since they are shared between the different URLRequestContexts.
  // The URLRequestContextStorage class manages dependent resources for a single
  // instance of URLRequestContext only.
  bool system_dependencies_initialized_;
  scoped_ptr<net::HostResolver> host_resolver_;
  scoped_ptr<net::ChannelIDService> channel_id_service_;
  scoped_ptr<net::CertVerifier> cert_verifier_;
  scoped_refptr<net::SSLConfigService> ssl_config_service_;
  scoped_ptr<net::TransportSecurityState> transport_security_state_;
  scoped_ptr<net::ProxyConfigService> proxy_config_service_;
  scoped_ptr<net::ProxyService> proxy_service_;
  scoped_ptr<net::HttpAuthHandlerFactory> http_auth_handler_factory_;
  scoped_ptr<net::HttpServerProperties> http_server_properties_;
  scoped_ptr<net::HttpUserAgentSettings> http_user_agent_settings_;
  scoped_ptr<net::HttpTransactionFactory> system_transaction_factory_;
  scoped_ptr<net::URLRequestJobFactory> system_job_factory_;

  bool main_dependencies_initialized_;
  scoped_ptr<net::HttpTransactionFactory> main_transaction_factory_;
  scoped_ptr<net::URLRequestJobFactory> main_job_factory_;

  bool media_dependencies_initialized_;
  scoped_ptr<net::HttpTransactionFactory> media_transaction_factory_;
};

}  // namespace shell
}  // namespace chromecast

#endif  // CHROMECAST_SHELL_BROWSER_URL_REQUEST_CONTEXT_FACTORY_H_
