import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'services/telemetry_service.dart';
import 'state/app_state.dart';
import 'screens/control_panel.dart';
import 'screens/runs_screen.dart';
import 'screens/dataset_analytics_screen.dart';
import 'screens/models_screen.dart';
import 'screens/inference_history_screen.dart';
import 'screens/scoring_results_screen.dart';
import 'widgets/context_bar.dart';
import 'utils/verbose_mode_storage.dart';

void main() {
  runApp(
    MultiProvider(
      providers: [
        Provider<TelemetryService>(create: (_) => TelemetryService()),
        ChangeNotifierProvider(create: (_) => AppState()),
      ],
      child: const TadsApp(),
    ),
  );
}

class TadsApp extends StatelessWidget {
  const TadsApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'TADS Dashboard',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF0F172A),
        primarySwatch: Colors.blue,
      ),
      home: const DashboardShell(),
    );
  }
}

class DashboardShell extends StatefulWidget {
  const DashboardShell({super.key});

  @override
  State<DashboardShell> createState() => _DashboardShellState();
}

class _DashboardShellState extends State<DashboardShell> with SingleTickerProviderStateMixin {
  late TabController _tabController;
  Timer? _jobPoller;
  bool _drawerOpen = false;
  final Set<String> _cancellingJobs = {};
  final Map<String, String> _jobErrors = {};

  @override
  void initState() {
    super.initState();
    final appState = context.read<AppState>();
    _tabController = TabController(length: 5, vsync: this, initialIndex: appState.currentTabIndex);
    _tabController.addListener(() {
      if (!_tabController.indexIsChanging) {
        appState.setTabIndex(_tabController.index);
      }
    });

    WidgetsBinding.instance.addPostFrameCallback((_) {
      _hydrateFromUrl();
    });
    _startJobPolling();
  }

  Future<void> _hydrateFromUrl() async {
    final uri = Uri.base;
    final appState = context.read<AppState>();
    final service = context.read<TelemetryService>();

    final verboseParam = _cleanParam(uri.queryParameters['verbose']);
    final verboseFromUrl = _parseVerbose(verboseParam);
    if (verboseFromUrl != null) {
      appState.setVerboseMode(verboseFromUrl);
      VerboseModeStorage.write(verboseFromUrl);
    } else {
      appState.setVerboseMode(VerboseModeStorage.read());
    }

    final dsId = _cleanParam(uri.queryParameters['datasetId']);
    final mId = _cleanParam(uri.queryParameters['modelId']);
    final metric = _cleanParam(uri.queryParameters['metric']);
    final warnings = <String>[];

    DatasetStatus? datasetStatus;
    ModelStatus? modelStatus;
    String? datasetToSet;
    String? modelToSet;

    if (dsId != null) {
      try {
        datasetStatus = await service.getDatasetStatus(dsId);
        datasetToSet = dsId;
      } catch (_) {
        warnings.add('Dataset from link not found: $dsId');
      }
    }

    if (mId != null) {
      try {
        modelStatus = await service.getModelStatus(mId);
        modelToSet = mId;
      } catch (_) {
        warnings.add('Model from link not found: $mId');
      }
    }

    if (!mounted) return;
    if (datasetToSet != null) {
      appState.setDataset(datasetToSet, status: datasetStatus);
    }
    if (modelToSet != null) {
      appState.setModel(modelToSet, status: modelStatus);
    }

    if (datasetToSet != null && metric != null) {
      final resolvedMetric = await _resolveMetric(metric);
      appState.setSelectedMetric(datasetToSet, resolvedMetric);
      if (resolvedMetric != metric) {
        warnings.add('Metric "$metric" not found. Defaulting to $resolvedMetric.');
      }
    }

    if (warnings.isNotEmpty && mounted) {
      for (final message in warnings) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(message),
            backgroundColor: Colors.orange,
            duration: const Duration(seconds: 4),
          ),
        );
      }
    }

    final resDsId = _cleanParam(uri.queryParameters['resultsDatasetId']);
    final resMId = _cleanParam(uri.queryParameters['resultsModelId']);
    if (resDsId != null && resMId != null) {
      bool resultsValid = true;
      try {
        await service.getDatasetStatus(resDsId);
        await service.getModelStatus(resMId);
      } catch (e) {
        resultsValid = false;
      }
      if (!mounted) return;
      if (!resultsValid) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Results link is invalid or outdated.'),
            backgroundColor: Colors.orange,
            duration: Duration(seconds: 4),
          ),
        );
        return;
      }
      final minScoreStr = _cleanParam(uri.queryParameters['minScore']);
      final onlyAnomStr = _cleanParam(uri.queryParameters['onlyAnomalies']);

      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (_) => ScoringResultsScreen(
            datasetId: resDsId,
            modelRunId: resMId,
            initialMinScore: minScoreStr != null ? double.tryParse(minScoreStr) : null,
            initialOnlyAnomalies: onlyAnomStr == 'true',
          ),
        ),
      );
    }
  }

  String? _cleanParam(String? value) {
    if (value == null) return null;
    final trimmed = value.trim();
    return trimmed.isEmpty ? null : trimmed;
  }

  bool? _parseVerbose(String? value) {
    if (value == null) return null;
    final normalized = value.toLowerCase();
    if (normalized == '1' || normalized == 'true' || normalized == 'yes') return true;
    if (normalized == '0' || normalized == 'false' || normalized == 'no') return false;
    return null;
  }

  Future<String> _resolveMetric(String metric) async {
    try {
      final service = context.read<TelemetryService>();
      final schema = await service.getMetricsSchema();
      final isValid = schema.any((m) => m['key'] == metric);
      return isValid ? metric : 'cpu_usage';
    } catch (_) {
      return metric;
    }
  }

  void _startJobPolling() {
    _jobPoller?.cancel();
    _jobPoller = Timer.periodic(const Duration(seconds: 5), (timer) async {
      final service = context.read<TelemetryService>();
      final appState = context.read<AppState>();
      
      final hasRunningJobs = appState.activeJobs.any((j) => j.status == 'RUNNING' || j.status == 'PENDING');
      
      if (_drawerOpen || hasRunningJobs) {
        try {
          final jobs = await service.listScoreJobs(limit: 10);
          appState.updateJobs(jobs);
          if (mounted) {
            setState(() {
              _cancellingJobs.removeWhere((id) =>
                  !jobs.any((j) => j.jobId == id) ||
                  !jobs.any((j) => j.jobId == id && (j.status == 'RUNNING' || j.status == 'PENDING')));
              _jobErrors.removeWhere((id, _) => !jobs.any((j) => j.jobId == id));
            });
          }
        } catch (e) {
          debugPrint('Job polling failed: $e');
        }
      }
    });
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final appState = context.watch<AppState>();
    if (_tabController.index != appState.currentTabIndex) {
      _tabController.animateTo(appState.currentTabIndex);
    }
  }

  @override
  void dispose() {
    _tabController.dispose();
    _jobPoller?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final appState = context.watch<AppState>();
    final runningJobs = appState.activeJobs.where((j) => j.status == 'RUNNING' || j.status == 'PENDING').length;

    return Scaffold(
      appBar: AppBar(
        backgroundColor: const Color(0xFF0F172A),
        title: const Text('TADS Dashboard', style: TextStyle(fontWeight: FontWeight.bold)),
        actions: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Text('Verbose Mode', style: TextStyle(fontSize: 12, color: Colors.white70)),
              Switch(
                value: appState.verboseMode,
                onChanged: (value) {
                  appState.setVerboseMode(value);
                  VerboseModeStorage.write(value);
                },
              ),
            ],
          ),
          Stack(
            alignment: Alignment.center,
            children: [
              Builder(
                builder: (context) => IconButton(
                  icon: const Icon(Icons.assignment_turned_in),
                  onPressed: () => Scaffold.of(context).openEndDrawer(),
                ),
              ),
              if (runningJobs > 0)
                Positioned(
                  right: 8,
                  top: 8,
                  child: Container(
                    padding: const EdgeInsets.all(2),
                    decoration: BoxDecoration(color: Colors.red, borderRadius: BorderRadius.circular(10)),
                    constraints: const BoxConstraints(minWidth: 16, minHeight: 16),
                    child: Text('$runningJobs',
                        style: const TextStyle(color: Colors.white, fontSize: 10),
                        textAlign: TextAlign.center),
                  ),
                ),
            ],
          ),
          const SizedBox(width: 16),
        ],
      ),
      onEndDrawerChanged: (isOpen) {
        setState(() => _drawerOpen = isOpen);
        if (isOpen) _startJobPolling();
      },
      endDrawer: _buildJobsDrawer(appState),
      body: Container(
        decoration: const BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: [Color(0xFF0F172A), Color(0xFF1E293B)],
          ),
        ),
        child: SafeArea(
          child: Column(
            children: [
              const SizedBox(height: 8),
              TabBar(
                controller: _tabController,
                isScrollable: true,
                tabs: const [
                  Tab(text: 'Control'),
                  Tab(text: 'Runs'),
                  Tab(text: 'Dataset Analytics'),
                  Tab(text: 'Training Runs'),
                  Tab(text: 'Inference History'),
                ],
              ),
              const SizedBox(height: 8),
              const ContextBar(),
              const SizedBox(height: 8),
              Expanded(
                child: TabBarView(
                  controller: _tabController,
                  children: const [
                    ControlPanel(),
                    RunsScreen(),
                    DatasetAnalyticsScreen(),
                    ModelsScreen(),
                    InferenceHistoryScreen(),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildJobsDrawer(AppState appState) {
    return Drawer(
      backgroundColor: const Color(0xFF1E293B),
      child: Column(
        children: [
          DrawerHeader(
            decoration: const BoxDecoration(color: Color(0xFF0F172A)),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Text('Scoring Jobs',
                    style: TextStyle(color: Colors.white, fontSize: 20, fontWeight: FontWeight.bold)),
                const SizedBox(height: 12),
                if (appState.activeJobs.any((j) => j.status == 'COMPLETED' || j.status == 'FAILED'))
                  TextButton.icon(
                    onPressed: appState.clearCompletedJobs,
                    icon: const Icon(Icons.clear_all, size: 16),
                    label: const Text('Clear Completed'),
                    style: TextButton.styleFrom(foregroundColor: Colors.white60),
                  ),
              ],
            ),
          ),
          Expanded(
            child: appState.activeJobs.isEmpty
                ? const Center(child: Text('No recent jobs'))
                : ListView.separated(
                    itemCount: appState.activeJobs.length,
                    separatorBuilder: (context, _) => const Divider(color: Colors.white12),
                    itemBuilder: (context, index) {
                      final job = appState.activeJobs[index];
                      return ListTile(
                        title: Text('Job: ${job.jobId.substring(0, 8)}...',
                            style: const TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
                        subtitle: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text('Status: ${job.status}',
                                style: TextStyle(color: _getJobStatusColor(job.status))),
                            if (_cancellingJobs.contains(job.jobId))
                              const Padding(
                                padding: EdgeInsets.only(top: 4),
                                child: Text('Cancelling...', style: TextStyle(color: Colors.orange, fontSize: 11)),
                              ),
                            if (job.status == 'RUNNING')
                              Padding(
                                padding: const EdgeInsets.only(top: 4),
                                child: LinearProgressIndicator(
                                  value: job.totalRows > 0 ? job.processedRows / job.totalRows : 0,
                                  backgroundColor: Colors.white10,
                                  valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF38BDF8)),
                                ),
                              ),
                            Text('${job.processedRows} / ${job.totalRows} rows',
                                style: const TextStyle(fontSize: 11, color: Colors.white54)),
                            if (job.error.isNotEmpty)
                              Text(job.error, style: const TextStyle(color: Colors.red, fontSize: 11)),
                            if (_jobErrors.containsKey(job.jobId))
                              Text(_jobErrors[job.jobId]!, style: const TextStyle(color: Colors.red, fontSize: 11)),
                            const SizedBox(height: 8),
                            Row(
                              children: [
                                TextButton(
                                  onPressed: () {
                                    appState.setDataset(job.datasetId);
                                    appState.setTabIndex(1); // Runs
                                    Navigator.pop(context);
                                  },
                                  child: const Text('Dataset', style: TextStyle(fontSize: 12)),
                                ),
                                TextButton(
                                  onPressed: () {
                                    appState.setModel(job.modelRunId);
                                    appState.setTabIndex(3); // Models
                                    Navigator.pop(context);
                                  },
                                  child: const Text('Model', style: TextStyle(fontSize: 12)),
                                ),
                                if (job.status == 'COMPLETED')
                                  TextButton.icon(
                                    onPressed: () {
                                      Navigator.push(
                                        context,
                                        MaterialPageRoute(
                                          builder: (_) => ScoringResultsScreen(
                                            datasetId: job.datasetId,
                                            modelRunId: job.modelRunId,
                                          ),
                                        ),
                                      );
                                    },
                                    icon: const Icon(Icons.analytics, size: 14),
                                    label: const Text('Results', style: TextStyle(fontSize: 12)),
                                  ),
                              ],
                            ),
                            if (job.status == 'RUNNING' || job.status == 'PENDING')
                              TextButton.icon(
                                onPressed: _cancellingJobs.contains(job.jobId)
                                    ? null
                                    : () => _cancelJob(job),
                                icon: const Icon(Icons.cancel, size: 14),
                                label: const Text('Cancel', style: TextStyle(fontSize: 12)),
                              ),
                          ],
                        ),
                        trailing: IconButton(
                          icon: const Icon(Icons.close, size: 16),
                          onPressed: () => appState.clearJob(job.jobId),
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
    );
  }

  Color _getJobStatusColor(String status) {
    switch (status) {
      case 'COMPLETED':
        return Colors.greenAccent;
      case 'FAILED':
        return Colors.redAccent;
      case 'CANCELLED':
        return Colors.orangeAccent;
      case 'RUNNING':
        return Colors.amberAccent;
      default:
        return Colors.white54;
    }
  }

  Future<void> _cancelJob(ScoreJobStatus job) async {
    final service = context.read<TelemetryService>();
    setState(() {
      _cancellingJobs.add(job.jobId);
      _jobErrors.remove(job.jobId);
    });
    try {
      await service.cancelJob(job.jobId);
    } catch (e) {
      if (!mounted) return;
      setState(() => _jobErrors[job.jobId] = 'Cancel failed: $e');
    } finally {
      if (mounted) {
        setState(() => _cancellingJobs.remove(job.jobId));
      }
    }
  }
}
