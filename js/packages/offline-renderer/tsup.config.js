import { defineConfig } from 'tsup';
import * as esbuild from 'esbuild';
import fs from 'node:fs';
import pkg from './package.json';

const LoadTextPlugin = {
  name: 'Load Raw Text',
  setup(build) {
    build.onLoad({ filter: /\/raw\/.*\.js/ }, async (args) => {
      let text = await fs.promises.readFile(args.path, 'utf8')
      return {
        contents: text.replace("__PKG_VERSION__", JSON.stringify(pkg.version)),
        loader: 'text',
      }
    })
  },
};

export default defineConfig({
  esbuildOptions(options) {
    options.define = Object.assign({}, options.define, {
      'process.env.PKG_VERSION': JSON.stringify(pkg.version),
    });
  },
  target: 'es2020',
  esbuildPlugins: [LoadTextPlugin],
})
