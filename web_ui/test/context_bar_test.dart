import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/state/app_state.dart';
import 'package:web_ui/widgets/context_bar.dart';

void main() {
  testWidgets('ContextBar renders null dataset/model state', (tester) async {
    await tester.pumpWidget(
      ChangeNotifierProvider(
        create: (_) => AppState(),
        child: const MaterialApp(
          home: Scaffold(
            body: ContextBar(),
          ),
        ),
      ),
    );

    expect(find.text('No dataset selected'), findsOneWidget);
    expect(find.text('No model selected'), findsOneWidget);
  });
}
