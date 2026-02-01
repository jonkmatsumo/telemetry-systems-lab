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

    // Hydrate from URL
    final uri = Uri.base;
    final dsId = uri.queryParameters['datasetId'];
    final mId = uri.queryParameters['modelId'];
    if (dsId != null) appState.setDataset(dsId);
    if (mId != null) appState.setModel(mId);

    _startJobPolling();
  }

  void _startJobPolling() {
    _jobPoller?.cancel();
    _jobPoller = Timer.periodic(const Duration(seconds: 5), (timer) async {
      final service = context.read<TelemetryService>();
      final appState = context.read<AppState>();
      
      final hasRunningJobs = appState.activeJobs.any((j) => j.status == 'RUNNING' || j.status == 'PENDING');
      
      // Only poll if drawer is open OR there are active jobs
      if (_drawerOpen || hasRunningJobs) {
        try {
          final jobs = await service.listScoreJobs(limit: 10);
          appState.updateJobs(jobs);
        } catch (_) {}
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
        if (isOpen) _startJobPolling(); // Ensure fresh data when opening
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
                  Tab(text: 'Models'),
                  Tab(text: 'Inference History'),
                ],
              ),
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
                    separatorBuilder: (_, __) => const Divider(color: Colors.white12),
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
                              ],
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
      case 'RUNNING':
        return Colors.amberAccent;
      default:
        return Colors.white54;
    }
  }
}
