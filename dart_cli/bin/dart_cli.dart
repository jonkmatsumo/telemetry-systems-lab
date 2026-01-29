import 'dart:convert';
import 'dart:io';
import 'package:http/http.dart' as http;
import 'package:args/args.dart';

const String baseUrl = 'http://localhost:8280';

void main(List<String> arguments) async {
  final parser = ArgParser()
    ..addCommand('generate')
    ..addCommand('train')
    ..addCommand('infer')
    ..addCommand('status')
    ..addCommand('full-test');

  final results = parser.parse(arguments);

  if (results.command == null) {
    print('Usage: dart_cli <command> [options]');
    print('Commands: generate, train, infer, status, full-test');
    return;
  }

  switch (results.command!.name) {
    case 'generate':
      await handleGenerate();
      break;
    case 'train':
      await handleTrain(results.command!.arguments);
      break;
    case 'infer':
      await handleInfer(results.command!.arguments);
      break;
    case 'status':
      await handleStatus(results.command!.arguments);
      break;
    case 'full-test':
      await runFullTest();
      break;
    default:
      print('Unknown command: ${results.command!.name}');
  }
}

Future<void> handleGenerate() async {
  print('Triggering dataset generation...');
  final resp = await http.post(
    Uri.parse('$baseUrl/datasets'),
    body: jsonEncode({'host_count': 5}),
    headers: {'Content-Type': 'application/json'},
  );
  print('Response: ${resp.statusCode} ${resp.body}');
}

Future<void> handleTrain(List<String> args) async {
  if (args.isEmpty) {
    print('Usage: train <dataset_id>');
    return;
  }
  final datasetId = args[0];
  print('Starting training for dataset $datasetId...');
  final resp = await http.post(
    Uri.parse('$baseUrl/train'),
    body: jsonEncode({'dataset_id': datasetId}),
    headers: {'Content-Type': 'application/json'},
  );
  print('Response: ${resp.statusCode} ${resp.body}');
}

Future<void> handleInfer(List<String> args) async {
  if (args.isEmpty) {
    print('Usage: infer <model_run_id>');
    return;
  }
  final modelId = args[0];
  print('Running inference with model $modelId...');
  final payload = {
    'model_run_id': modelId,
    'samples': [
      {
        'cpu_usage': 95.0,
        'memory_usage': 90.0,
        'disk_utilization': 30.0,
        'network_rx_rate': 10.0,
        'network_tx_rate': 5.0
      },
      {
        'cpu_usage': 40.0,
        'memory_usage': 50.0,
        'disk_utilization': 30.0,
        'network_rx_rate': 10.0,
        'network_tx_rate': 5.0
      }
    ]
  };

  final resp = await http.post(
    Uri.parse('$baseUrl/inference'),
    body: jsonEncode(payload),
    headers: {'Content-Type': 'application/json'},
  );
  print('Response: ${resp.statusCode} ${resp.body}');
}

Future<void> handleStatus(List<String> args) async {
   if (args.isEmpty) {
    print('Usage: status <job_type: dataset|model> <id>');
    return;
  }
  final type = args[0];
  final id = args[1];
  final endpoint = type == 'dataset' ? 'datasets' : 'train';
  
  final resp = await http.get(Uri.parse('$baseUrl/$endpoint/$id'));
  print('Status of $type $id: ${resp.statusCode} ${resp.body}');
}

Future<void> runFullTest() async {
  print('--- STAGE 1: GENERATE ---');
  final genResp = await http.post(
    Uri.parse('$baseUrl/datasets'),
    body: jsonEncode({'host_count': 2}),
    headers: {'Content-Type': 'application/json'},
  );
  final datasetId = jsonDecode(genResp.body)['run_id'];
  print('Dataset ID: $datasetId');

  print('\n--- STAGE 2: POLLING GENERATE ---');
  while (true) {
    final statusResp = await http.get(Uri.parse('$baseUrl/datasets/$datasetId'));
    final status = jsonDecode(statusResp.body)['status'];
    print('Current Status: $status');
    if (status == 'SUCCEEDED' || status == 'COMPLETED') break;
    if (status == 'FAILED') throw Exception('Generation failed');
    await Future.delayed(Duration(seconds: 2));
  }

  print('\n--- STAGE 3: TRAIN ---');
  final trainResp = await http.post(
    Uri.parse('$baseUrl/train'),
    body: jsonEncode({'dataset_id': datasetId}),
    headers: {'Content-Type': 'application/json'},
  );
  final modelId = jsonDecode(trainResp.body)['model_run_id'];
  print('Model Run ID: $modelId');

  print('\n--- STAGE 4: POLLING TRAIN ---');
  while (true) {
    final statusResp = await http.get(Uri.parse('$baseUrl/train/$modelId'));
    final status = jsonDecode(statusResp.body)['status'];
    print('Current Status: $status');
    if (status == 'COMPLETED') break;
    if (status == 'FAILED') throw Exception('Training failed');
    await Future.delayed(Duration(seconds: 2));
  }

  print('\n--- STAGE 5: INFERENCE ---');
  await handleInfer([modelId]);
  print('\nFull Test Complete.');
}
