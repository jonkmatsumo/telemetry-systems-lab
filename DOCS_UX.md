# TADS UX Regression Checklist

This document serves as a guide for manual verification of UX improvements in the Telemetry Anomaly Detection System (TADS).

## Known issues fixed in this PR (Repro Notes)
- [x] **RD-01:** "Load into Inference Preview" fails to pass record data.
  *   *Repro:* Open Results → record drawer → Click "Load into Inference Preview". Verify dataset/model is selected but record values are not populated in the Control tab.
- [ ] **SR-01:** Min score slider has hardcoded max of 10.
  *   *Repro:* Open Scoring Results for a dataset with high scores. Verify slider cannot filter scores > 10.
- [ ] **DL-01:** Deep link `metric` param is ignored.
  *   *Repro:* Copy analytics link with `&metric=network_tx_rate`, reload page. Verify metric reverts to default (`cpu_usage`).
- [ ] **DL-02:** Scoring Results screen does not support deep linking.
  *   *Repro:* Refresh while on Scoring Results screen. Verify it redirects to home/default state instead of restoring results view.
- [ ] **ERR-01:** Silent error handling masking failures.
  *   *Repro:* Disconnect server or API. Observe UI swallows errors in polling/hydration without user feedback.

## 1. Unified Context & State Persistence
- [ ] **AppState Hydration:** Select a dataset in the `Runs` tab, navigate to the `Control` tab. The "Model Training" section should be enabled and show the selected dataset ID.
- [ ] **Bidirectional Updates:** Generate a dataset in the `Control` tab. Once it reaches `SUCCEEDED`, navigate to the `Runs` tab. The new dataset should appear at the top and be selected.
- [ ] **Tab Context:** Refresh the page. Navigate between tabs. The selected dataset and model should persist across navigation (while the app is running).
- [ ] **Disabled State Messaging:** When no dataset is selected, the "Model Training" card in the `Control` tab should show "Select a dataset to enable training" in amber text.

## 2. Resource Selectors
- [ ] **Dataset Selector:** In the `Control` tab, use the dropdown in "1. Data Generation" to pick an existing dataset. Observe that "2. Model Training" enables appropriately.
- [ ] **Model Selector:** In the `Control` tab, use the dropdown in "2. Model Training" to pick an existing model. Observe that "3. Inference Preview" enables appropriately.
- [ ] **Refresh Sync:** Click the "Refresh Status" button in the `Control` tab. The dropdown lists should update with any newly created resources.

## 3. Dynamic Analytics
- [ ] **Metric Selection:** In the `Dataset Analytics` tab, select a dataset. Use the "Metric" dropdown to switch between `cpu_usage`, `memory_usage`, etc.
- [ ] **Chart Updates:** Verify that the Histogram and Time-Series charts update to show the selected metric.
- [ ] **Per-Dataset Memory:** Switch between two datasets with different selected metrics. The app should remember which metric was last viewed for each dataset.

## 4. Cross-Linking & Shortcuts
- [ ] **Runs → Train:** In `Runs` detail, click "Train Model on this Dataset". It should switch to the `Control` tab with that dataset selected and the training section focused.
- [ ] **Runs → Analytics:** In `Runs` detail, click "View Analytics". It should switch to the `Dataset Analytics` tab with that dataset selected.
- [ ] **Models → Inference:** In `Models` detail, click "Inference Preview". It should switch to the `Control` tab with that model selected and the inference section focused.
- [ ] **List Linkage:** Verify that the `Models` list shows the source dataset ID for each model.

## 5. Global Job Visibility
- [ ] **Jobs Drawer:** Click the checklist icon in the top right. The "Scoring Jobs" drawer should open.
- [ ] **Real-time Progress:** Start a "Score Dataset" job from the `Models` tab. Open the global Jobs drawer. You should see a progress bar updating as rows are processed.
- [ ] **Status Badge:** When jobs are running, a red badge with the count of active jobs should appear on the Jobs icon in the AppBar.
- [ ] **Failure Details:** If a job fails, the error message should be visible in the Jobs drawer.

## 6. UX Hardening & Resilience
- [ ] **Stale Selection Handling:** If a dataset or model is deleted (or missing from latest fetch), the `Control` tab should clear the selection and show an amber warning banner.
- [ ] **Empty States:** When no datasets or models exist, the selectors in the `Control` tab should show helpful empty state messages (e.g., "No datasets available...").
- [ ] **Schema-Driven Metrics:** Verify that the metric list in `Dataset Analytics` is fetched from the server. If the server is down, it should fall back to a safe default list.
- [ ] **Analytics Error Recovery:** If a dataset doesn't support a specific metric, an inline error alert should appear within the analytics view, allowing for a retry.
- [ ] **Jobs Polling Efficiency:** The app should only poll for job updates when the Jobs drawer is open or there are active/pending jobs.
- [ ] **Job Drawer Actions:** Verify that "Dataset" and "Model" buttons in the Jobs drawer correctly navigate and set the application context.
- [ ] **Clear Completed Jobs:** Verify that the "Clear Completed" button in the Jobs drawer correctly hides finished jobs.
- [ ] **Inference Preview with Real Data:** In the `Control` tab, once a model is trained and a dataset is selected, you should be able to pick a real record from a dropdown to test anomaly detection.
- [x] **Inference Handoff:** Open Results → record drawer → “Load into Inference Preview”. Verify dataset/model selected, record loaded (message shown), and inference results displayed.

## 7. Dataset Schema & Explorer
- [ ] **Feature List:** In `Runs` detail, click the "Features" tab. You should see a list of available metrics with descriptions, types, and units.
- [ ] **Quick Stats:** Click a feature in the list. A panel should appear showing count, mean, min, max, p50, and p95 for that metric in the selected dataset.

## 8. Feature Distributions Browser
- [ ] **Distributions Tab:** In `Dataset Analytics`, click the "Distributions" tab.
- [ ] **Metric Rail:** Use the left rail to switch between different metrics. Histogram and Trend charts should update accordingly.
- [ ] **Comparison Mode:** Click the "+" icon next to a metric in the rail. It should render a second set of charts and stats side-by-side for comparison.
- [ ] **Suggested Metrics:** Verify that the "Suggested (High Var)" chips appear and correctly switch the primary metric when clicked.

## 9. Model ↔ Dataset Linkage
- [ ] **Dataset → Models:** In `Runs` detail, "Models" tab, verify that models trained on that dataset are listed with a "View Model" shortcut.
- [ ] **Model → Scored Datasets:** In `Models` detail, "Scored Datasets" tab, verify that datasets scored by that model are listed with a "View Dataset" shortcut.

## 10. Scoring Results Browser
- [ ] **Open Results:** From a completed job in the Jobs drawer, or from the "Scored Datasets" tab in Models, navigate to the Results browser.
- [ ] **Table View:** Verify that scored records are listed with their anomaly scores and predictions.
- [ ] **Filters:** Toggle "Only Anomalies" and adjust the "Min Score" slider. The table should update to reflect the filters.
- [ ] **Record Detail:** Click a row to open the detail drawer. Verify the data and click "Load into Inference Preview" to ensure it navigates correctly.

## 11. Deep Links
- [ ] **Copy Link:** Click "Copy Link" in any detail or analytics screen.
- [ ] **Hydration:** Paste the link into a new tab (or refresh). Verify that the app restores the selected dataset, model, and metric automatically.
