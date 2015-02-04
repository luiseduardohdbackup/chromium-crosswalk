// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ANDROID_INFOBARS_INFOBAR_CONTAINER_ANDROID_H_
#define CHROME_BROWSER_UI_ANDROID_INFOBARS_INFOBAR_CONTAINER_ANDROID_H_

#include <map>
#include <string>

#include "base/android/jni_weak_ref.h"
#include "base/android/scoped_java_ref.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "components/infobars/core/infobar_container.h"

class InfoBarAndroid;

namespace content {
class WebContents;
}

class InfoBarContainerAndroid : public infobars::InfoBarContainer {
 public:
  InfoBarContainerAndroid(JNIEnv* env,
                          jobject infobar_container);
  void Destroy(JNIEnv* env, jobject obj);

  JavaObjectWeakGlobalRef java_container() const {
    return weak_java_infobar_container_;
  }

 private:
  ~InfoBarContainerAndroid() override;

  // InfobarContainer:
  void PlatformSpecificAddInfoBar(infobars::InfoBar* infobar,
                                  size_t position) override;
  void PlatformSpecificRemoveInfoBar(infobars::InfoBar* infobar) override;
  void PlatformSpecificReplaceInfoBar(infobars::InfoBar* old_infobar,
                                      infobars::InfoBar* new_infobar) override;

  // Create the Java equivalent of |android_bar| and add it to the java
  // container.
  void AttachJavaInfoBar(InfoBarAndroid* android_bar);

  // We're owned by the java infobar, need to use a weak ref so it can destroy
  // us.
  JavaObjectWeakGlobalRef weak_java_infobar_container_;

  DISALLOW_COPY_AND_ASSIGN(InfoBarContainerAndroid);
};

// Registers the InfoBarContainer's native methods through JNI.
bool RegisterInfoBarContainer(JNIEnv* env);

#endif  // CHROME_BROWSER_UI_ANDROID_INFOBARS_INFOBAR_CONTAINER_ANDROID_H_
