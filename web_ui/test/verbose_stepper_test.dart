import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/utils/verbose_steps.dart';
import 'package:web_ui/widgets/verbose_stepper.dart';

void main() {
  testWidgets('VerboseStepper renders steps and artifacts', (tester) async {
    final steps = [
      VerboseStep(
        id: 's1',
        title: 'Plan',
        status: 'ok',
        offsetMs: 0,
        durationMs: 12,
        artifacts: [
          VerboseArtifact(label: 'Inputs', payload: '{"foo": "bar"}'),
        ],
        traceId: 't1',
        spanId: 's1',
      ),
    ];

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: VerboseStepper(steps: steps, traceBaseUrl: 'http://trace'),
        ),
      ),
    );

    expect(find.text('Plan'), findsOneWidget);
    await tester.tap(find.text('Plan'));
    await tester.pumpAndSettle();

    expect(find.text('Inputs'), findsOneWidget);
    expect(find.textContaining('foo'), findsOneWidget);
  });
}
