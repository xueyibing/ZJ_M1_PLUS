// Resolve lv_font_conv from a local install or npm's global package directory.
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const candidates = [];
if (process.env.LV_FONT_CONV) {
  candidates.push(process.env.LV_FONT_CONV);
}
candidates.push(path.resolve(__dirname, '..', 'node_modules', 'lv_font_conv', 'lv_font_conv.js'));

try {
  const npmRoot = execFileSync('npm', ['root', '-g'], { encoding: 'utf8' }).trim();
  candidates.push(path.join(npmRoot, 'lv_font_conv', 'lv_font_conv.js'));
} catch {
  // npm may not be on PATH; an explicit LV_FONT_CONV path can still be used.
}

const converter = candidates.find(candidate => fs.existsSync(candidate));
if (!converter) {
  throw new Error('lv_font_conv not found. Install it locally/globally or set LV_FONT_CONV.');
}

module.exports = converter;
