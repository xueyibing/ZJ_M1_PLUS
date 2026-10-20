// Regenerate the compact Chinese font used by the brightness setting label.
const { spawnSync } = require('child_process');

const conv = 'C:\\Users\\93160\\AppData\\Roaming\\npm\\node_modules\\lv_font_conv\\lv_font_conv.js';
const out = 'D:\\gitee\\ZJ_M1\\UITouch3.5\\font\\ui_brightness_font_12.c';
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
