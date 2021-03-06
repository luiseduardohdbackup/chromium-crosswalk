// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/common/mac/app_mode_chrome_locator.h"

#include <CoreFoundation/CoreFoundation.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_version_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// This needs to be a browser test because it expects to find a Chrome.app
// bundle in the output directory.

// Return the path to the Chrome/Chromium app bundle compiled along with the
// test executable.
void GetChromeBundlePath(base::FilePath* chrome_bundle) {
  base::FilePath path;
  PathService::Get(base::DIR_MODULE, &path);
  path = path.Append(chrome::kBrowserProcessExecutableName);
  path = path.ReplaceExtension(base::FilePath::StringType("app"));
  *chrome_bundle = path;
}

}  // namespace

TEST(ChromeLocatorTest, FindBundle) {
  base::FilePath finder_bundle_path;
  EXPECT_TRUE(
      app_mode::FindBundleById(@"com.apple.finder", &finder_bundle_path));
  EXPECT_TRUE(base::DirectoryExists(finder_bundle_path));
}

TEST(ChromeLocatorTest, FindNonExistentBundle) {
  base::FilePath dummy;
  EXPECT_FALSE(app_mode::FindBundleById(@"this.doesnt.exist", &dummy));
}

TEST(ChromeLocatorTest, GetNonExistentBundleInfo) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath executable_path;
  base::FilePath version_path;
  base::FilePath framework_path;
  EXPECT_FALSE(app_mode::GetChromeBundleInfo(temp_dir.path(),
                                             std::string(),
                                             &executable_path,
                                             &version_path,
                                             &framework_path));
}

TEST(ChromeLocatorTest, GetChromeBundleInfo) {
  base::FilePath chrome_bundle_path;
  GetChromeBundlePath(&chrome_bundle_path);
  ASSERT_TRUE(base::DirectoryExists(chrome_bundle_path));

  base::FilePath executable_path;
  base::FilePath version_path;
  base::FilePath framework_path;
  EXPECT_TRUE(app_mode::GetChromeBundleInfo(chrome_bundle_path,
                                            std::string(),
                                            &executable_path,
                                            &version_path,
                                            &framework_path));
  EXPECT_TRUE(base::PathExists(executable_path));
  EXPECT_TRUE(base::DirectoryExists(version_path));
  EXPECT_TRUE(base::PathExists(framework_path));
}

TEST(ChromeLocatorTest, GetChromeBundleInfoWithLatestVersion) {
  base::FilePath chrome_bundle_path;
  GetChromeBundlePath(&chrome_bundle_path);
  ASSERT_TRUE(base::DirectoryExists(chrome_bundle_path));

  base::FilePath executable_path;
  base::FilePath version_path;
  base::FilePath framework_path;
  EXPECT_TRUE(app_mode::GetChromeBundleInfo(chrome_bundle_path,
                                            chrome::VersionInfo().Version(),
                                            &executable_path,
                                            &version_path,
                                            &framework_path));
  EXPECT_TRUE(base::PathExists(executable_path));
  EXPECT_TRUE(base::DirectoryExists(version_path));
  EXPECT_TRUE(base::PathExists(framework_path));
}

TEST(ChromeLocatorTest, GetChromeBundleInfoWithInvalidVersion) {
  base::FilePath chrome_bundle_path;
  GetChromeBundlePath(&chrome_bundle_path);
  ASSERT_TRUE(base::DirectoryExists(chrome_bundle_path));

  base::FilePath executable_path;
  base::FilePath version_path;
  base::FilePath framework_path;
  // This still passes because it should default to the latest version.
  EXPECT_TRUE(app_mode::GetChromeBundleInfo(chrome_bundle_path,
                                            std::string("invalid_version"),
                                            &executable_path,
                                            &version_path,
                                            &framework_path));
  EXPECT_TRUE(base::PathExists(executable_path));
  EXPECT_TRUE(base::DirectoryExists(version_path));
  EXPECT_TRUE(base::PathExists(framework_path));
}

TEST(ChromeLocatorTest, GetChromeBundleInfoWithPreviousVersion) {
  base::FilePath chrome_bundle_path;
  GetChromeBundlePath(&chrome_bundle_path);
  ASSERT_TRUE(base::DirectoryExists(chrome_bundle_path));

  // Make a symlink that pretends to be a previous version.
  base::FilePath fake_version_directory = chrome_bundle_path.Append("Contents")
                                              .Append("Versions")
                                              .Append("previous_version");
  EXPECT_TRUE(base::CreateSymbolicLink(
      base::FilePath(chrome::VersionInfo().Version()), fake_version_directory));

  base::FilePath executable_path;
  base::FilePath version_path;
  base::FilePath framework_path;
  EXPECT_TRUE(app_mode::GetChromeBundleInfo(chrome_bundle_path,
                                            std::string("previous_version"),
                                            &executable_path,
                                            &version_path,
                                            &framework_path));
  EXPECT_TRUE(base::PathExists(executable_path));
  EXPECT_TRUE(base::DirectoryExists(version_path));
  EXPECT_TRUE(base::PathExists(framework_path));

  base::DeleteFile(fake_version_directory, false);
}
