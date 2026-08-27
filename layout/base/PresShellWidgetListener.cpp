/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PresShellWidgetListener.h"

#include "WindowRenderer.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/StartupTimeline.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/widget/Screen.h"
#include "nsContentUtils.h"  // for nsAutoScriptBlocker
#include "nsDeviceContext.h"
#include "nsPresContext.h"
#include "nsDocShell.h"
#include "nsIFrame.h"
#include "nsIWidget.h"
#include "nsIWidgetListener.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsRefreshDriver.h"
#include "nsXULPopupManager.h"

using namespace mozilla;
using namespace mozilla::widget;

static uint32_t gLastUserEventTime = 0;
static void MaybeUpdateLastUserEventTime(WidgetGUIEvent* aEvent) {
  WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
  if ((mouseEvent &&
       // Ignore mouse events that we synthesize.
       mouseEvent->mReason == WidgetMouseEvent::eReal &&
       // Ignore mouse exit and enter (we'll get moves if the user
       // is really moving the mouse) since we get them when we
       // create and destroy widgets.
       mouseEvent->mMessage != eMouseExitFromWidget &&
       mouseEvent->mMessage != eMouseEnterIntoWidget) ||
      aEvent->HasKeyEventMessage() || aEvent->HasIMEEventMessage()) {
    gLastUserEventTime = PR_IntervalToMicroseconds(PR_IntervalNow());
  }
}

uint32_t PresShellWidgetListener::GetLastUserEventTime() {
  return gLastUserEventTime;
}

PresShellWidgetListener::PresShellWidgetListener(PresShell* aPs)
    : mPresShell(aPs) {
  MOZ_COUNT_CTOR(PresShellWidgetListener);
}

PresShellWidgetListener::~PresShellWidgetListener() {
  MOZ_COUNT_DTOR(PresShellWidgetListener);

  if (mPreviousWindow) {
    mPreviousWindow->SetPreviouslyAttachedWidgetListener(nullptr);
  }

  // Destroy and release the widget
  DetachWidget();
}

void PresShellWidgetListener::DetachWidget() {
  if (mWindow) {
    mWindow->SetAttachedWidgetListener(nullptr);
    mWindow = nullptr;
  }
}

// Attach to a top level widget and start receiving mirrored events.
void PresShellWidgetListener::AttachToTopLevelWidget(nsIWidget* aWidget) {
  MOZ_ASSERT(aWidget, "null widget ptr");
  MOZ_ASSERT(!aWidget->GetWidgetListener() ||
                 aWidget->GetWidgetListener()->GetAppWindow(),
             "Expect a top level widget");

  /// XXXjimm This is a temporary workaround to an issue w/document
  // viewer (bug 513162).
  if (nsIWidgetListener* listener = aWidget->GetAttachedWidgetListener()) {
    if (auto* old = listener->GetAsPresShellWidgetListener()) {
      old->DetachFromTopLevelWidget();
    }
  }

  mWindow = aWidget;

  mWindow->SetAttachedWidgetListener(this);
  if (mWindow->GetWindowType() != WindowType::Invisible) {
    mWindow->AsyncEnableDragDrop(true);
  }
  if (nsPresContext* pc = mPresShell->GetPresContext()) {
    pc->RefreshDriver()->NotifyWidgetAttached();
  }
}

// Detach us from an attached widget.
void PresShellWidgetListener::DetachFromTopLevelWidget() {
  MOZ_ASSERT(mWindow, "null mWindow for DetachFromTopLevelWidget!");

  mWindow->SetAttachedWidgetListener(nullptr);
  if (nsIWidgetListener* listener =
          mWindow->GetPreviouslyAttachedWidgetListener()) {
    if (auto* previousListener = listener->GetAsPresShellWidgetListener()) {
      // Ensure the listener doesn't think it's being used anymore
      previousListener->mPreviousWindow = nullptr;
    }
  }

  // If the new pres shell is paint suppressed then the window
  // will want to use us instead until that's done
  mWindow->SetPreviouslyAttachedWidgetListener(this);
  mPreviousWindow = std::move(mWindow);
  MOZ_ASSERT(!mWindow);
}

PresShell* PresShellWidgetListener::GetPresShell() { return mPresShell; }

void PresShellWidgetListener::WindowResized(nsIWidget*,
                                            const LayoutDeviceIntSize& aSize) {
  RefPtr<PresShell> ps = GetPresShell();
  if (!ps) {
    return;
  }

  nsPresContext* pc = ps->GetPresContext();
  if (!pc) {
    return;
  }

  // ensure DPI is up-to-date, in case of window being opened and sized
  // on a non-default-dpi display (bug 829963)
  pc->DeviceContext()->CheckDPIChange();
  int32_t p2a = pc->AppUnitsPerDevPixel();
  if (auto* frame = ps->GetRootFrame()) {
    // Usually the resize would deal with this, but there are some cases (like
    // web-extension popups) where frames might already be correctly sized etc
    // due to a call to e.g. nsDocumentViewer::GetContentSize or so.
    frame->InvalidateFrame();
  }
  ps->SetLayoutViewportSize(LayoutDeviceIntSize::ToAppUnits(aSize, p2a),
                            /* aDelay = */ false);

  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    pm->AdjustPopupsOnWindowChange(ps);
  }
}

#ifdef MOZ_WIDGET_ANDROID
// With e10s off -- which is how GeckoView runs under the Android Translation
// Layer -- the tab is in this process and there is no BrowserParent to forward
// the dynamic toolbar state to.  Layout would then never learn that a dynamic
// toolbar exists while the compositor does, and the compositor's fixed layer
// margin would be applied with nothing to cancel it, pushing position:fixed
// bottom content off the bottom of the window.  Find the root content pres
// context in this process instead.
static nsPresContext* InProcessRootContentPresContext(dom::Document& aDoc) {
  if (nsPresContext* pc = aDoc.GetPresContext()) {
    if (pc->IsRootContentDocumentCrossProcess()) {
      return pc;
    }
  }
  nsPresContext* found = nullptr;
  aDoc.EnumerateSubDocuments([&found](dom::Document& aSubDoc) {
    found = InProcessRootContentPresContext(aSubDoc);
    return found ? CallState::Stop : CallState::Continue;
  });
  return found;
}

nsPresContext* PresShellWidgetListener::GetInProcessContentPresContext() {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return nullptr;
  }
  dom::Document* document = presShell->GetDocument();
  return document ? InProcessRootContentPresContext(*document) : nullptr;
}
#endif

void PresShellWidgetListener::DynamicToolbarMaxHeightChanged(
    ScreenIntCoord aHeight) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Should be only called for the browser parent process");
  bool sentToRemote = false;
  CallOnAllRemoteChildren(
      [aHeight, &sentToRemote](dom::BrowserParent* aBrowserParent) -> CallState {
        sentToRemote = true;
        aBrowserParent->DynamicToolbarMaxHeightChanged(aHeight);
        return CallState::Continue;
      });
#ifdef MOZ_WIDGET_ANDROID
  if (!sentToRemote) {
    if (nsPresContext* pc = GetInProcessContentPresContext()) {
      pc->SetDynamicToolbarMaxHeight(aHeight);
    }
  }
#endif
}

void PresShellWidgetListener::DynamicToolbarOffsetChanged(
    ScreenIntCoord aOffset) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Should be only called for the browser parent process");
  bool sentToRemote = false;
  CallOnAllRemoteChildren(
      [aOffset, &sentToRemote](dom::BrowserParent* aBrowserParent) -> CallState {
        // Skip background tabs.
        if (!aBrowserParent->GetDocShellIsActive()) {
          return CallState::Continue;
        }

        sentToRemote = true;
        aBrowserParent->DynamicToolbarOffsetChanged(aOffset);
        return CallState::Stop;
      });
#ifdef MOZ_WIDGET_ANDROID
  if (!sentToRemote) {
    nsPresContext* pc = GetInProcessContentPresContext();
    if (!pc) {
      return;
    }
    // The max height reaches us before the content document exists, so it can
    // be lost; take it from the widget here as well.
    if (pc->GetDynamicToolbarMaxHeight() == 0) {
      if (nsIWidget* widget = GetWidget()) {
        pc->SetDynamicToolbarMaxHeight(widget->GetDynamicToolbarMaxHeight());
      }
    }
    ScreenIntCoord maxHeight = pc->GetDynamicToolbarMaxHeight();
    ScreenIntCoord offset = aOffset;
    if (offset > 0) {
      offset = ScreenIntCoord(0);
    } else if (offset < -maxHeight) {
      offset = -maxHeight;
    }
    pc->UpdateDynamicToolbarOffset(offset);
  }
#endif
}

void PresShellWidgetListener::KeyboardHeightChanged(ScreenIntCoord aHeight) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Should be only called for the browser parent process");
#ifdef MOZ_WIDGET_ANDROID
  CallOnAllRemoteChildren(
      [aHeight](dom::BrowserParent* aBrowserParent) -> CallState {
        // Skip background tabs.
        if (!aBrowserParent->GetDocShellIsActive()) {
          return CallState::Continue;
        }

        aBrowserParent->KeyboardHeightChanged(aHeight);
        return CallState::Stop;
      });
#endif
}

void PresShellWidgetListener::AndroidPipModeChanged(bool aPipMode) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Should be only called for the browser parent process");
#ifdef MOZ_WIDGET_ANDROID
  CallOnAllRemoteChildren(
      [aPipMode](dom::BrowserParent* aBrowserParent) -> CallState {
        aBrowserParent->AndroidPipModeChanged(aPipMode);
        return CallState::Continue;
      });
#endif
}

void PresShellWidgetListener::PaintWindow(nsIWidget* aWidget) {
  RefPtr ps = GetPresShell();
  if (!ps) {
    return;
  }
  RefPtr renderer = aWidget->GetWindowRenderer();
  if (!renderer->NeedsWidgetInvalidation()) {
    ps->PaintSynchronously();
    renderer->FlushRendering(wr::RenderReasons::WIDGET);
  } else {
    ps->SyncPaintFallback(ps->GetRootFrame(), renderer);
  }
  mozilla::StartupTimeline::RecordOnce(mozilla::StartupTimeline::FIRST_PAINT);
  ps->DidPaintWindow();
}

void PresShellWidgetListener::DidCompositeWindow(
    mozilla::layers::TransactionId aTransactionId,
    const TimeStamp& aCompositeStart, const TimeStamp& aCompositeEnd) {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  nsAutoScriptBlocker scriptBlocker;

  nsPresContext* context = presShell->GetPresContext();
  nsRootPresContext* rootContext = context->GetRootPresContext();
  if (rootContext) {
    rootContext->NotifyDidPaintForSubtree(aTransactionId, aCompositeEnd);
  }

  mozilla::StartupTimeline::RecordOnce(mozilla::StartupTimeline::FIRST_PAINT2,
                                       aCompositeEnd);
}

nsEventStatus PresShellWidgetListener::HandleEvent(WidgetGUIEvent* aEvent) {
  MOZ_ASSERT(aEvent->mWidget, "null widget ptr");

  nsEventStatus result = nsEventStatus_eIgnore;
  MaybeUpdateLastUserEventTime(aEvent);
  if (RefPtr<PresShell> ps = GetPresShell()) {
    if (nsIFrame* root = ps->GetRootFrame()) {
      ps->HandleEvent(root, aEvent, false, &result);
    }
  }
  return result;
}

void PresShellWidgetListener::SafeAreaInsetsChanged(
    const LayoutDeviceIntMargin& aSafeAreaInsets) {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  LayoutDeviceIntMargin windowSafeAreaInsets;
  const LayoutDeviceIntRect windowRect = mWindow->GetScreenBounds();
  if (nsCOMPtr<nsIScreen> screen = mWindow->GetWidgetScreen()) {
    windowSafeAreaInsets = nsContentUtils::GetWindowSafeAreaInsets(
        screen, aSafeAreaInsets, windowRect);
  }

  presShell->GetPresContext()->SetSafeAreaInsets(windowSafeAreaInsets);

  // https://github.com/w3c/csswg-drafts/issues/4670
  // Actually we don't set this value on sub document. This behaviour is
  // same as Blink.
  CallOnAllRemoteChildren(
      [windowSafeAreaInsets](dom::BrowserParent* aBrowserParent) -> CallState {
        (void)aBrowserParent->SendSafeAreaInsetsChanged(windowSafeAreaInsets);
        return CallState::Continue;
      });
}

bool PresShellWidgetListener::IsPrimaryFramePaintSuppressed() const {
  return StaticPrefs::layout_show_previous_page() && mPresShell &&
         mPresShell->IsPaintingSuppressed();
}

void PresShellWidgetListener::CallOnAllRemoteChildren(
    const std::function<CallState(dom::BrowserParent*)>& aCallback) {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  dom::Document* document = presShell->GetDocument();
  if (!document) {
    return;
  }

  nsPIDOMWindowOuter* window = document->GetWindow();
  if (!window) {
    return;
  }

  nsContentUtils::CallOnAllRemoteChildren(window, aCallback);
}
