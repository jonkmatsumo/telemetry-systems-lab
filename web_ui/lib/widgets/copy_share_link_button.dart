import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import '../state/app_state.dart';
import '../utils/share_link.dart';

class CopyShareLinkButton extends StatelessWidget {
  final ShareLinkScope scope;
  final Map<String, String> overrideParams;
  final String tooltip;
  final String label;
  final bool showLabel;

  const CopyShareLinkButton({
    super.key,
    this.scope = ShareLinkScope.fullContext,
    this.overrideParams = const {},
    this.tooltip = 'Copy link',
    this.label = 'Copy Link',
    this.showLabel = true,
  });

  Future<void> _copy(BuildContext context) async {
    final appState = context.read<AppState>();
    final base = Uri.base;
    final baseShare = buildShareUri(state: appState, baseUri: base, scope: scope);
    final merged = Map<String, String>.from(baseShare.queryParameters)..addAll(overrideParams);
    final shareUri = baseShare.replace(queryParameters: merged);

    try {
      await Clipboard.setData(ClipboardData(text: shareUri.toString()));
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Link copied')), 
      );
    } catch (e) {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Failed to copy link: $e'), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final icon = IconButton(
      icon: const Icon(Icons.link),
      tooltip: tooltip,
      onPressed: () => _copy(context),
    );

    if (!showLabel) {
      return icon;
    }

    return OutlinedButton.icon(
      onPressed: () => _copy(context),
      icon: const Icon(Icons.link),
      label: Text(label),
    );
  }
}
