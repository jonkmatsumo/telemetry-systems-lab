import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/widgets/charts.dart';

void main() {
  testWidgets('LineChart shows and hides tooltip on hover', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: SizedBox(
            width: 200,
            height: 120,
            child: LineChart(
              x: [0, 1],
              y: [10, 20],
            ),
          ),
        ),
      ),
    );

    final rect = tester.getRect(find.byType(LineChart));
    final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
    await mouse.addPointer();
    await mouse.moveTo(Offset(rect.right - 10, rect.center.dy));
    await tester.pump();

    expect(find.text('x: 1.00'), findsOneWidget);
    expect(find.text('y: 20.00'), findsOneWidget);

    await mouse.moveTo(const Offset(-10, -10));
    await tester.pump();

    expect(find.text('x: 1.00'), findsNothing);
  });

  testWidgets('BarChart shows tooltip on hover', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: SizedBox(
            width: 200,
            height: 120,
            child: BarChart(
              values: [5, 10],
              labels: ['A', 'B'],
            ),
          ),
        ),
      ),
    );

    final rect = tester.getRect(find.byType(BarChart));
    final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
    await mouse.addPointer();
    await mouse.moveTo(Offset(rect.left + 20, rect.center.dy));
    await tester.pump();

    expect(find.text('x: A'), findsOneWidget);
    expect(find.text('y: 5.00'), findsOneWidget);
  });
}
