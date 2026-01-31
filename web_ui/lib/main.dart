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
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
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
              const SizedBox(height: 16),
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
}
