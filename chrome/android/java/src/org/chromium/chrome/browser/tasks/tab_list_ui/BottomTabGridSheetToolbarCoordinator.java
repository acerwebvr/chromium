// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_list_ui;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.lifecycle.Destroyable;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;

/**
 * Coordinator for the toolbar component that will be shown on top of the tab
 * grid when presented inside the bottom sheet. {@link BottomTabGridCoordinator}
 */
class BottomTabGridSheetToolbarCoordinator implements Destroyable {
    private final BottomTabGridSheetToolbarView mToolbarView;
    private final PropertyModelChangeProcessor mModelChangeProcessor;

    /**
     * Construct a new {@link BottomTabGridSheetToolbarCoordinator}.
     *
     * @param context              The {@link Context} used to retrieve resources.
     * @param parentView           The parent {@link View} to which the content will
     *                             eventually be attached.
     * @param toolbarPropertyModel The {@link PropertyModel} instance representing
     *                             the toolbar.
     */
    BottomTabGridSheetToolbarCoordinator(
            Context context, ViewGroup parentView, PropertyModel toolbarPropertyModel) {
        mToolbarView = (BottomTabGridSheetToolbarView) LayoutInflater.from(context).inflate(
                R.layout.bottom_tab_grid_toolbar, parentView, false);
        mModelChangeProcessor = PropertyModelChangeProcessor.create(
                toolbarPropertyModel, mToolbarView, BottomTabGridSheetToolbarViewBinder::bind);
    }

    /** @return The content {@link View}. */
    View getView() {
        return mToolbarView;
    }

    /** Destroy the toolbar component. */
    @Override
    public void destroy() {
        mModelChangeProcessor.destroy();
    }
}
