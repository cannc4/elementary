#!/usr/bin/env node

import fs from 'fs';
import path from 'path';

const SRC_DIR = path.join(process.cwd(), 'src');

function fixFile(filePath) {
  const content = fs.readFileSync(filePath, 'utf8');
  const fixed = content.replace(
    /from\s+'rescript\/lib\/es6\\curry\.js';/g,
    "from 'rescript/lib/es6/curry.js';"
  );
  if (fixed !== content) {
    fs.writeFileSync(filePath, fixed, 'utf8');
    console.log(`Fixed import in ${path.basename(filePath)}`);
  }
}

if (fs.existsSync(SRC_DIR)) {
  const files = fs.readdirSync(SRC_DIR).filter((f) => f.endsWith('.gen.ts'));
  for (const f of files) {
    fixFile(path.join(SRC_DIR, f));
  }
}


