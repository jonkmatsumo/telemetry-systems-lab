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

class DashboardShell extends StatelessWidget {
  const DashboardShell({super.key});

  @override
  Widget build(BuildContext context) {
    return DefaultTabController(
      length: 5,
      child: Scaffold(
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
                const TabBar(
                  isScrollable: true,
                  tabs: [
                    Tab(text: 'Control'),
                    Tab(text: 'Runs'),
                    Tab(text: 'Dataset Analytics'),
                    Tab(text: 'Models'),
                    Tab(text: 'Inference History'),
                  ],
                ),
                const SizedBox(height: 8),
                const Expanded(
                  child: TabBarView(
                    children: [
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
      ),
    );
  }
}
