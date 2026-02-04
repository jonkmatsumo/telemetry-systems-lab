import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/state/app_state.dart';
import 'package:web_ui/widgets/copy_share_link_button.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('CopyShareLinkButton shows snackbar on copy', (tester) async {
    final messenger = TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    messenger.setMockMethodCallHandler(SystemChannels.platform, (call) async {
      if (call.method == 'Clipboard.setData') {
        return null;
      }
      return null;
    });

    await tester.pumpWidget(
      MultiProvider(
        providers: [
          ChangeNotifierProvider(create: (_) => AppState()),
        ],
        child: const MaterialApp(
          home: Scaffold(
            body: CopyShareLinkButton(),
          ),
        ),
      ),
    );

    await tester.tap(find.byType(OutlinedButton));
    await tester.pump();

    expect(find.text('Link copied'), findsOneWidget);

    messenger.setMockMethodCallHandler(SystemChannels.platform, null);
  });
}
