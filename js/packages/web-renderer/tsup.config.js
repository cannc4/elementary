import { defineConfig } from 'tsup';
import * as esbuild from 'esbuild';
import fs from 'node:fs';
import pkg from './package.json';

const LoadTextPlugin = {
  name: 'Load Raw Text',
  setup(build) {
    build.onLoad({ filter: /.*/ }, async (args) => {
      if ((args.path.includes('/raw/') || args.path.includes('\\raw\\')) && args.path.endsWith('.js')) {
        let text = await fs.promises.readFile(args.path, 'utf8')
        const processedText = text.replace("__PKG_VERSION__", JSON.stringify(pkg.version));
        return {
          contents: `export default ${JSON.stringify(processedText)};`,
          loader: 'js',
        }
      }
    })
  },
};

export default defineConfig({
  esbuildPlugins: [LoadTextPlugin],
  esbuildOptions(options) {
    options.define = Object.assign({}, options.define, {
      'process.env.PKG_VERSION': JSON.stringify(pkg.version),
    });
  },
  dts: {
    compilerOptions: {
      skipLibCheck: true,
    },
  },
})
