// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

login.createScreen('OAuthEnrollmentScreen', 'oauth-enrollment', function() {
  /** @const */ var STEP_SIGNIN = 'signin';
  /** @const */ var STEP_WORKING = 'working';
  /** @const */ var STEP_ERROR = 'error';
  /** @const */ var STEP_SUCCESS = 'success';

  /** @const */ var HELP_TOPIC_ENROLLMENT = 4631259;

  return {
    EXTERNAL_API: [
      'showStep',
      'showError',
      'doReload',
    ],

    /**
     * URL to load in the sign in frame.
     */
    signInUrl_: null,

    /**
     * The current step. This is the last value passed to showStep().
     */
    currentStep_: null,

    /**
     * Opaque token used to correlate request and response while retrieving the
     * authenticated user's e-mail address from GAIA.
     */
    attemptToken_: null,

    /** @override */
    decorate: function() {
      window.addEventListener('message',
                              this.onMessage_.bind(this), false);
      $('oauth-enroll-error-retry').addEventListener('click',
                                                     this.doRetry_.bind(this));
      $('oauth-enroll-learn-more-link').addEventListener(
          'click',
          function() {
            chrome.send('launchHelpApp', [HELP_TOPIC_ENROLLMENT]);
          });

      this.updateLocalizedContent();
    },

    /**
     * Updates localized strings.
     */
    updateLocalizedContent: function() {
      $('oauth-enroll-re-enrollment-text').innerHTML =
          loadTimeData.getStringF(
              'oauthEnrollReEnrollmentText',
              '<b id="oauth-enroll-management-domain"></b>');
      $('oauth-enroll-management-domain').textContent = this.managementDomain_;
      $('oauth-enroll-re-enrollment-text').hidden = !this.managementDomain_;
    },

    /**
     * Header text of the screen.
     * @type {string}
     */
    get header() {
      return loadTimeData.getString('oauthEnrollScreenTitle');
    },

    /**
     * Buttons in oobe wizard's button strip.
     * @type {array} Array of Buttons.
     */
    get buttons() {
      var buttons = [];
      var ownerDocument = this.ownerDocument;

      function makeButton(id, classes, label, handler) {
        var button = ownerDocument.createElement('button');
        button.id = id;
        button.classList.add('oauth-enroll-button');
        button.classList.add.apply(button.classList, classes);
        button.textContent = label;
        button.addEventListener('click', handler);
        buttons.push(button);
      }

      makeButton(
          'oauth-enroll-cancel-button',
          ['oauth-enroll-focus-on-error'],
          loadTimeData.getString('oauthEnrollCancel'),
          function() {
            chrome.send('oauthEnrollClose', ['cancel']);
          });

      makeButton(
          'oauth-enroll-back-button',
          ['oauth-enroll-focus-on-error'],
          loadTimeData.getString('oauthEnrollBack'),
          function() {
            $('oauth-enroll-back-button').disabled = true;

            $('oauth-enroll-back-button').
                classList.add('preserve-disabled-state');

            chrome.send('oauthEnrollClose', ['cancel']);
          });

      makeButton(
          'oauth-enroll-retry-button',
          ['oauth-enroll-focus-on-error'],
          loadTimeData.getString('oauthEnrollRetry'),
          this.doRetry_.bind(this));

      makeButton(
          'oauth-enroll-done-button',
          ['oauth-enroll-focus-on-success'],
          loadTimeData.getString('oauthEnrollDone'),
          function() {
            chrome.send('oauthEnrollClose', ['done']);
          });

      return buttons;
    },

    /**
     * Event handler that is invoked just before the frame is shown.
     * @param {Object} data Screen init payload, contains the signin frame
     * URL.
     */
    onBeforeShow: function(data) {
      var url = data.signin_url;
      url += '?gaiaUrl=' + encodeURIComponent(data.gaiaUrl);
      url += '&needPassword=0';
      this.signInUrl_ = url;
      var modes = ['manual', 'forced', 'recovery'];
      for (var i = 0; i < modes.length; ++i) {
        this.classList.toggle('mode-' + modes[i],
                              data.enrollment_mode == modes[i]);
      }
      this.managementDomain_ = data.management_domain;
      $('oauth-enroll-signin-frame').contentWindow.location.href =
          this.signInUrl_;
      this.updateLocalizedContent();
      this.showStep(STEP_SIGNIN);
    },

    /**
     * Cancels enrollment and drops the user back to the login screen.
     */
    cancel: function() {
      chrome.send('oauthEnrollClose', ['cancel']);
    },

    /**
     * Switches between the different steps in the enrollment flow.
     * @param {string} step the steps to show, one of "signin", "working",
     * "error", "success".
     */
    showStep: function(step) {
      this.classList.toggle('oauth-enroll-state-' + this.currentStep_, false);
      this.classList.toggle('oauth-enroll-state-' + step, true);
      var focusElements =
          this.querySelectorAll('.oauth-enroll-focus-on-' + step);
      for (var i = 0; i < focusElements.length; ++i) {
        if (getComputedStyle(focusElements[i])['display'] != 'none') {
          focusElements[i].focus();
          break;
        }
      }
      this.currentStep_ = step;
    },

    /**
     * Sets an error message and switches to the error screen.
     * @param {string} message the error message.
     * @param {boolean} retry whether the retry link should be shown.
     */
    showError: function(message, retry) {
      $('oauth-enroll-error-message').textContent = message;
      $('oauth-enroll-error-retry').hidden = !retry;
      this.showStep(STEP_ERROR);
    },

    doReload: function() {
      $('oauth-enroll-signin-frame').contentWindow.location.href =
          this.signInUrl_;
    },

    /**
     * Retries the enrollment process after an error occurred in a previous
     * attempt. This goes to the C++ side through |chrome| first to clean up the
     * profile, so that the next attempt is performed with a clean state.
     */
    doRetry_: function() {
      chrome.send('oauthEnrollRetry');
    },

    /**
     * Checks if a given HTML5 message comes from the URL loaded into the signin
     * frame.
     * @param {Object} m HTML5 message.
     * @type {boolean} whether the message comes from the signin frame.
     */
    isSigninMessage_: function(m) {
      return this.signInUrl_ != null &&
          this.signInUrl_.indexOf(m.origin) == 0 &&
          m.source == $('oauth-enroll-signin-frame').contentWindow;
    },

    /**
     * Event handler for HTML5 messages.
     * @param {Object} m HTML5 message.
     */
    onMessage_: function(m) {
      if (!this.isSigninMessage_(m))
        return;

      var msg = m.data;

      if (msg.method == 'completeLogin') {
        // A user has successfully authenticated via regular GAIA or SAML.
        chrome.send('oauthEnrollCompleteLogin', [msg.email]);
      }

      if (msg.method == 'authPageLoaded' && this.currentStep_ == STEP_SIGNIN) {
        if (msg.isSAML) {
          $('oauth-saml-notice-message').textContent = loadTimeData.getStringF(
              'samlNotice',
              msg.domain);
        }
        this.classList.toggle('saml', msg.isSAML);
      }

      if (msg.method == 'resetAuthFlow') {
        this.classList.remove('saml');
      }

      if (msg.method == 'loginUILoaded' && this.currentStep_ == STEP_SIGNIN) {
        $('oauth-enroll-back-button').disabled = false;

        $('oauth-enroll-back-button').
            classList.remove('preserve-disabled-state');

        chrome.send('frameLoadingCompleted', [0]);
      }

      if (msg.method == 'insecureContentBlocked') {
        this.showError(
            loadTimeData.getStringF('insecureURLEnrollmentError', msg.url),
            false);
      }

      if (msg.method == 'missingGaiaInfo') {
        this.showError(
            loadTimeData.getString('fatalEnrollmentError'),
            false);
      }
    }
  };
});

