// Regenerate the compact Chinese font used by the brightness setting label.
const { spawnSync } = require('child_process');
const path = require('path');

const conv = require('./font_conv_path');
const out = path.resolve(__dirname, '..', 'font', 'ui_brightness_font_12.c');
const font = 'C:\\Windows\\Fonts\\simhei.ttf';

const args = [
  conv,
  '--no-compress', '--no-prefilter',
  '--bpp', '4',
  '--size', '17',
  '--font', font,
  '--symbols', '\u4eae\u5ea6',
  '--format', 'lvgl',
  '--lv-font-name', 'ui_brightness_font_12',
  '--force-fast-kern-format',
  '-o', out,
];

const result = spawnSync('node', args, { stdio: 'inherit' });
process.exit(result.status === null ? 1 : result.status);
