#!/usr/bin/env node

import fs from 'fs';
import path from 'path';
import { spawnSync } from 'child_process';
import { fileURLToPath } from 'url';

const pkgDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const rootDir = path.resolve(pkgDir, '../../..');
const outputPath = path.join(pkgDir, 'elementary-wasm.cjs');

function ensureDocker() {
  const res = spawnSync('docker', ['--version'], { stdio: 'ignore', shell: false });
  if (res.status !== 0) {
    console.error('Docker is required to prebuild WASM. Please install Docker Desktop.');
    process.exit(1);
  }
}

function runDockerBuild() {
  const dockerArgs = [
    'run',
    '-w', '/src',
    '-v', `${rootDir}:/src`,
    ...(process.env.EXTERNAL_DIR ? ['-v', `${process.env.EXTERNAL_DIR}:${process.env.EXTERNAL_DIR}`] : []),
    ...(process.env.EXTERNAL_INCLUDE ? ['-v', `${process.env.EXTERNAL_INCLUDE}:${process.env.EXTERNAL_INCLUDE}`] : []),
    '-e', `ELEM_BUILD_ASYNC=1`,
    '-e', `EXTERNAL_MODULES=${process.env.EXTERNAL_MODULES || ''}`,
    '-e', `EXTERNAL_INCLUDE=${process.env.EXTERNAL_INCLUDE || ''}`,
    'docker.io/emscripten/emsdk:3.1.52',
    '/bin/bash', '-lc', './scripts/build-wasm.sh build'
  ];

  const res = spawnSync('docker', dockerArgs, { stdio: 'inherit', shell: false });
  if (res.status !== 0) {
    process.exit(res.status || 1);
  }
}

function copyOutput() {
  const builtFile = path.join(rootDir, 'build', 'out', 'elementary-wasm.js');
  if (!fs.existsSync(builtFile)) {
    console.error(`Expected build output not found: ${builtFile}`);
    process.exit(1);
  }
  fs.copyFileSync(builtFile, outputPath);
  console.log(`WASM prebuild copied to ${outputPath}`);
}

ensureDocker();
runDockerBuild();
copyOutput();


