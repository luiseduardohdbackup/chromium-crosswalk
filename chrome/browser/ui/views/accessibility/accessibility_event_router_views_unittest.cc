// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/accessibility/accessibility_extension_api.h"
#include "chrome/browser/accessibility/accessibility_extension_api_constants.h"
#include "chrome/browser/ui/views/accessibility/accessibility_event_router_views.h"
#include "chrome/test/base/testing_profile.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/native_widget.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

using base::ASCIIToUTF16;

class AccessibilityViewsDelegate : public views::TestViewsDelegate {
 public:
  AccessibilityViewsDelegate() {}
  ~AccessibilityViewsDelegate() override {}

  // Overridden from views::TestViewsDelegate:
  void NotifyAccessibilityEvent(views::View* view,
                                ui::AXEvent event_type) override {
    AccessibilityEventRouterViews::GetInstance()->HandleAccessibilityEvent(
        view, event_type);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(AccessibilityViewsDelegate);
};

class AccessibilityWindowDelegate : public views::WidgetDelegate {
 public:
  explicit AccessibilityWindowDelegate(views::View* contents)
      : contents_(contents) { }

  // Overridden from views::WidgetDelegate:
  void DeleteDelegate() override { delete this; }
  views::View* GetContentsView() override { return contents_; }
  const views::Widget* GetWidget() const override {
    return contents_->GetWidget();
  }
  views::Widget* GetWidget() override { return contents_->GetWidget(); }

 private:
  views::View* contents_;

  DISALLOW_COPY_AND_ASSIGN(AccessibilityWindowDelegate);
};

class ViewWithNameAndRole : public views::View {
 public:
  explicit ViewWithNameAndRole(const base::string16& name,
                               ui::AXRole role)
      : name_(name),
        role_(role) {
  }

  void GetAccessibleState(ui::AXViewState* state) override {
    views::View::GetAccessibleState(state);
    state->name = name_;
    state->role = role_;
  }

  void set_name(const base::string16& name) { name_ = name; }

 private:
  base::string16 name_;
  ui::AXRole role_;
  DISALLOW_COPY_AND_ASSIGN(ViewWithNameAndRole);
};

class AccessibilityEventRouterViewsTest : public views::ViewsTestBase {
 public:
  AccessibilityEventRouterViewsTest() : control_event_count_(0) {}

  void SetUp() override {
    set_views_delegate(new AccessibilityViewsDelegate);
    views::ViewsTestBase::SetUp();
    EnableAccessibilityAndListenToFocusNotifications();
  }

  void TearDown() override {
    ClearCallback();
    views::ViewsTestBase::TearDown();
  }

  views::Widget* CreateWindowWithContents(views::View* contents) {
    views::Widget* widget = views::Widget::CreateWindowWithContextAndBounds(
        new AccessibilityWindowDelegate(contents),
        GetContext(),
        gfx::Rect(0, 0, 500, 500));

    // Create a profile and associate it with this window.
    widget->SetNativeWindowProperty(Profile::kProfileKey, &profile_);

    return widget;
  }

  void EnableAccessibilityAndListenToFocusNotifications() {
    // Switch on accessibility event notifications.
    ExtensionAccessibilityEventRouter* accessibility_event_router =
        ExtensionAccessibilityEventRouter::GetInstance();
    accessibility_event_router->SetAccessibilityEnabled(true);
    accessibility_event_router->SetControlEventCallbackForTesting(base::Bind(
        &AccessibilityEventRouterViewsTest::OnControlEvent,
        base::Unretained(this)));
  }

  void ClearCallback() {
    ExtensionAccessibilityEventRouter* accessibility_event_router =
        ExtensionAccessibilityEventRouter::GetInstance();
    accessibility_event_router->ClearControlEventCallback();
  }

 protected:
  // Handle Focus event.
  virtual void OnControlEvent(ui::AXEvent event,
                            const AccessibilityControlInfo* info) {
    control_event_count_++;
    last_control_type_ = info->type();
    last_control_name_ = info->name();
    last_control_context_ = info->context();
  }

  int control_event_count_;
  std::string last_control_type_;
  std::string last_control_name_;
  std::string last_control_context_;
  TestingProfile profile_;
};

TEST_F(AccessibilityEventRouterViewsTest, TestFocusNotification) {
  const char kButton1ASCII[] = "Button1";
  const char kButton2ASCII[] = "Button2";
  const char kButton3ASCII[] = "Button3";
  const char kButton3NewASCII[] = "Button3New";

  // Create a contents view with 3 buttons.
  views::View* contents = new views::View();
  views::LabelButton* button1 = new views::LabelButton(
      NULL, ASCIIToUTF16(kButton1ASCII));
  button1->SetStyle(views::Button::STYLE_BUTTON);
  contents->AddChildView(button1);
  views::LabelButton* button2 = new views::LabelButton(
      NULL, ASCIIToUTF16(kButton2ASCII));
  button2->SetStyle(views::Button::STYLE_BUTTON);
  contents->AddChildView(button2);
  views::LabelButton* button3 = new views::LabelButton(
      NULL, ASCIIToUTF16(kButton3ASCII));
  button3->SetStyle(views::Button::STYLE_BUTTON);
  contents->AddChildView(button3);

  // Put the view in a window.
  views::Widget* window = CreateWindowWithContents(contents);
  window->Show();

  // Set focus to the first button initially and run message loop to execute
  // callback.
  button1->RequestFocus();
  base::MessageLoop::current()->RunUntilIdle();

  // Change the accessible name of button3.
  button3->SetAccessibleName(ASCIIToUTF16(kButton3NewASCII));

  // Advance focus to the next button and test that we got the
  // expected notification with the name of button 2.
  views::FocusManager* focus_manager = contents->GetWidget()->GetFocusManager();
  control_event_count_ = 0;
  focus_manager->AdvanceFocus(false);
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_EQ(1, control_event_count_);
  EXPECT_EQ(kButton2ASCII, last_control_name_);

  // Advance to button 3. Expect the new accessible name we assigned.
  focus_manager->AdvanceFocus(false);
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_EQ(2, control_event_count_);
  EXPECT_EQ(kButton3NewASCII, last_control_name_);

  // Advance to button 1 and check the notification.
  focus_manager->AdvanceFocus(false);
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_EQ(3, control_event_count_);
  EXPECT_EQ(kButton1ASCII, last_control_name_);

  window->CloseNow();
}

TEST_F(AccessibilityEventRouterViewsTest, TestToolbarContext) {
  const char kToolbarNameASCII[] = "MyToolbar";
  const char kButtonNameASCII[] = "MyButton";

  // Create a toolbar with a button.
  views::View* contents = new ViewWithNameAndRole(
      ASCIIToUTF16(kToolbarNameASCII),
      ui::AX_ROLE_TOOLBAR);
  views::LabelButton* button = new views::LabelButton(
      NULL, ASCIIToUTF16(kButtonNameASCII));
  button->SetStyle(views::Button::STYLE_BUTTON);
  contents->AddChildView(button);

  // Put the view in a window.
  views::Widget* window = CreateWindowWithContents(contents);

  // Set focus to the button.
  control_event_count_ = 0;
  button->RequestFocus();

  base::MessageLoop::current()->RunUntilIdle();

  // Test that we got the event with the expected name and context.
  EXPECT_EQ(1, control_event_count_);
  EXPECT_EQ(kButtonNameASCII, last_control_name_);
  EXPECT_EQ(kToolbarNameASCII, last_control_context_);

  window->CloseNow();
}

TEST_F(AccessibilityEventRouterViewsTest, TestAlertContext) {
  const char kAlertTextASCII[] = "MyAlertText";
  const char kButtonNameASCII[] = "MyButton";

  // Create an alert with static text and a button, similar to an infobar.
  views::View* contents = new ViewWithNameAndRole(
      base::string16(),
      ui::AX_ROLE_ALERT);
  views::Label* label = new views::Label(ASCIIToUTF16(kAlertTextASCII));
  contents->AddChildView(label);
  views::LabelButton* button = new views::LabelButton(
      NULL, ASCIIToUTF16(kButtonNameASCII));
  button->SetStyle(views::Button::STYLE_BUTTON);
  contents->AddChildView(button);

  // Put the view in a window.
  views::Widget* window = CreateWindowWithContents(contents);

  // Set focus to the button.
  control_event_count_ = 0;
  button->RequestFocus();

  base::MessageLoop::current()->RunUntilIdle();

  // Test that we got the event with the expected name and context.
  EXPECT_EQ(1, control_event_count_);
  EXPECT_EQ(kButtonNameASCII, last_control_name_);
  EXPECT_EQ(kAlertTextASCII, last_control_context_);

  window->CloseNow();
}

TEST_F(AccessibilityEventRouterViewsTest, StateChangeAfterNotification) {
  const char kContentsNameASCII[] = "Contents";
  const char kOldNameASCII[] = "OldName";
  const char kNewNameASCII[] = "NewName";

  // Create a toolbar with a button.
  views::View* contents = new ViewWithNameAndRole(
      ASCIIToUTF16(kContentsNameASCII),
      ui::AX_ROLE_CLIENT);
  ViewWithNameAndRole* child = new ViewWithNameAndRole(
      ASCIIToUTF16(kOldNameASCII),
      ui::AX_ROLE_BUTTON);
  child->SetFocusable(true);
  contents->AddChildView(child);

  // Put the view in a window.
  views::Widget* window = CreateWindowWithContents(contents);

  // Set focus to the child view.
  control_event_count_ = 0;
  child->RequestFocus();

  // Change the child's name after the focus notification.
  child->set_name(ASCIIToUTF16(kNewNameASCII));

  // We shouldn't get the notification right away.
  EXPECT_EQ(0, control_event_count_);

  // Process anything in the event loop. Now we should get the notification,
  // and it should give us the new control name, not the old one.
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_EQ(1, control_event_count_);
  EXPECT_EQ(kNewNameASCII, last_control_name_);

  window->CloseNow();
}

TEST_F(AccessibilityEventRouterViewsTest, NotificationOnDeletedObject) {
  const char kContentsNameASCII[] = "Contents";
  const char kNameASCII[] = "OldName";

  // Create a toolbar with a button.
  views::View* contents = new ViewWithNameAndRole(
      ASCIIToUTF16(kContentsNameASCII),
      ui::AX_ROLE_CLIENT);
  ViewWithNameAndRole* child = new ViewWithNameAndRole(
      ASCIIToUTF16(kNameASCII),
      ui::AX_ROLE_BUTTON);
  child->SetFocusable(true);
  contents->AddChildView(child);

  // Put the view in a window.
  views::Widget* window = CreateWindowWithContents(contents);

  // Set focus to the child view.
  control_event_count_ = 0;
  child->RequestFocus();

  // Delete the child!
  delete child;

  // We shouldn't get the notification right away.
  EXPECT_EQ(0, control_event_count_);

  // Process anything in the event loop. We shouldn't get a notification
  // because the view is no longer valid, and this shouldn't crash.
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_EQ(0, control_event_count_);

  window->CloseNow();
}

TEST_F(AccessibilityEventRouterViewsTest, AlertsFromWindowAndControl) {
  const char kButtonASCII[] = "Button";
  const char* kTypeAlert = extension_accessibility_api_constants::kTypeAlert;
  const char* kTypeWindow = extension_accessibility_api_constants::kTypeWindow;

  // Create a contents view with a button.
  views::View* contents = new views::View();
  views::LabelButton* button = new views::LabelButton(
      NULL, ASCIIToUTF16(kButtonASCII));
  button->SetStyle(views::Button::STYLE_BUTTON);
  contents->AddChildView(button);

  // Put the view in a window.
  views::Widget* window = CreateWindowWithContents(contents);
  window->Show();

  // Send an alert event from the button and let the event loop run.
  control_event_count_ = 0;
  button->NotifyAccessibilityEvent(ui::AX_EVENT_ALERT, true);
  base::MessageLoop::current()->RunUntilIdle();

  EXPECT_EQ(kTypeAlert, last_control_type_);
  EXPECT_EQ(1, control_event_count_);
  EXPECT_EQ(kButtonASCII, last_control_name_);

  // Send an alert event from the window and let the event loop run.
  control_event_count_ = 0;
  window->GetRootView()->NotifyAccessibilityEvent(
      ui::AX_EVENT_ALERT, true);
  base::MessageLoop::current()->RunUntilIdle();

  EXPECT_EQ(1, control_event_count_);
  EXPECT_EQ(kTypeWindow, last_control_type_);

  window->CloseNow();
}

TEST_F(AccessibilityEventRouterViewsTest, AccessibilityFocusableView) {
  // Create a view with a child view.
  views::View* parent = new views::View();
  views::View* child = new views::View();
  parent->AddChildView(child);

  // Put the view in a window.
  views::Widget* window = CreateWindowWithContents(parent);

  // Since the child view has no accessibility focusable ancestors, this
  // should still be the child view.
  views::View* accessible_view =
      AccessibilityEventRouterViews::FindFirstAccessibleAncestor(child);
  EXPECT_EQ(accessible_view, child);

  // Now make the parent view accessibility focusable. Calling
  // FindFirstAccessibleAncestor() again on child should return the parent
  // view.
  parent->SetAccessibilityFocusable(true);
  accessible_view =
      AccessibilityEventRouterViews::FindFirstAccessibleAncestor(child);
  EXPECT_EQ(accessible_view, parent);

  window->CloseNow();
}

namespace {

class SimpleMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  enum {
    IDC_MENU_ITEM_1,
    IDC_MENU_ITEM_2,
    IDC_MENU_INVISIBLE,
    IDC_MENU_ITEM_3,
  };

  SimpleMenuDelegate() {}
  ~SimpleMenuDelegate() override {}

  views::MenuItemView* BuildMenu() {
    menu_model_.reset(new ui::SimpleMenuModel(this));
    menu_model_->AddItem(IDC_MENU_ITEM_1, ASCIIToUTF16("Item 1"));
    menu_model_->AddItem(IDC_MENU_ITEM_2, ASCIIToUTF16("Item 2"));
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    menu_model_->AddItem(IDC_MENU_INVISIBLE, ASCIIToUTF16("Invisible"));
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    menu_model_->AddItem(IDC_MENU_ITEM_3, ASCIIToUTF16("Item 3"));

    menu_adapter_.reset(new views::MenuModelAdapter(menu_model_.get()));
    views::MenuItemView* menu_view = menu_adapter_->CreateMenu();
    menu_runner_.reset(new views::MenuRunner(menu_view, 0));
    return menu_view;
  }

  bool IsCommandIdChecked(int command_id) const override { return false; }

  bool IsCommandIdEnabled(int command_id) const override { return true; }

  bool IsCommandIdVisible(int command_id) const override {
    return command_id != IDC_MENU_INVISIBLE;
  }

  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) override {
    return false;
  }

  void ExecuteCommand(int command_id, int event_flags) override {}

 private:
  scoped_ptr<ui::SimpleMenuModel> menu_model_;
  scoped_ptr<views::MenuModelAdapter> menu_adapter_;
  scoped_ptr<views::MenuRunner> menu_runner_;

  DISALLOW_COPY_AND_ASSIGN(SimpleMenuDelegate);
};

}  // namespace

TEST_F(AccessibilityEventRouterViewsTest, MenuIndexAndCountForInvisibleMenu) {
  SimpleMenuDelegate menu_delegate;
  views::MenuItemView* menu = menu_delegate.BuildMenu();
  views::View* menu_container = menu->CreateSubmenu();

  struct TestCase {
    int command_id;
    int expected_index;
    int expected_count;
  } kTestCases[] = {
    { SimpleMenuDelegate::IDC_MENU_ITEM_1, 0, 3 },
    { SimpleMenuDelegate::IDC_MENU_ITEM_2, 1, 3 },
    { SimpleMenuDelegate::IDC_MENU_INVISIBLE, 0, 3 },
    { SimpleMenuDelegate::IDC_MENU_ITEM_3, 2, 3 },
  };

  for (size_t i = 0; i < arraysize(kTestCases); ++i) {
    int index = 0;
    int count = 0;

    AccessibilityEventRouterViews::RecursiveGetMenuItemIndexAndCount(
        menu_container,
        menu->GetMenuItemByID(kTestCases[i].command_id),
        &index,
        &count);
    EXPECT_EQ(kTestCases[i].expected_index, index) << "Case " << i;
    EXPECT_EQ(kTestCases[i].expected_count, count) << "Case " << i;
  }
}
