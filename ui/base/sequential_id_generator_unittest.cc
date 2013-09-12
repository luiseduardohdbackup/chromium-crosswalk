// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/sequential_id_generator.h"

#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ui {

typedef testing::Test SequentialIDGeneratorTest;

TEST(SequentialIDGeneratorTest, AddRemove) {
  const uint32 kMinID = 2;
  SequentialIDGenerator generator(kMinID);

  EXPECT_EQ(2U, generator.GetGeneratedID(45));
  EXPECT_EQ(3U, generator.GetGeneratedID(23));
  EXPECT_EQ(2U, generator.GetGeneratedID(45));
  EXPECT_TRUE(generator.HasGeneratedIDFor(45));
  EXPECT_TRUE(generator.HasGeneratedIDFor(23));

  generator.ReleaseGeneratedID(2);
  EXPECT_FALSE(generator.HasGeneratedIDFor(45));
  EXPECT_TRUE(generator.HasGeneratedIDFor(23));
  EXPECT_EQ(3U, generator.GetGeneratedID(23));

  EXPECT_FALSE(generator.HasGeneratedIDFor(1));
  EXPECT_EQ(2U, generator.GetGeneratedID(1));
  EXPECT_TRUE(generator.HasGeneratedIDFor(1));

  generator.ReleaseGeneratedID(3);
  EXPECT_EQ(3U, generator.GetGeneratedID(45));
  EXPECT_TRUE(generator.HasGeneratedIDFor(45));

  generator.ReleaseNumber(45);
  EXPECT_FALSE(generator.HasGeneratedIDFor(45));
}

}  // namespace ui
