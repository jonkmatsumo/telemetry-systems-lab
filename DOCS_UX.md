# TADS UX Regression Checklist

This document serves as a guide for manual verification of UX improvements in the Telemetry Anomaly Detection System (TADS).

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
