// Generate ui_egg_name_34 for the four Chinese flavor names.
// Uses spawnSync with array args so Chinese characters are passed as UTF-8.
const { spawnSync } = require('child_process');

const conv = 'C:\\Users\\93160\\AppData\\Roaming\\npm\\node_modules\\lv_font_conv\\lv_font_conv.js';
const out = 'D:\\gitee\\ZJ_M1\\UITouch3.5\\font\\ui_egg_name_34.c';
const font = 'C:\\Windows\\Fonts\\simhei.ttf';

const symbols = '全熟糖心软嫩太阳蛋';

const args = [
  conv,
  '--no-compress', '--no-prefilter',
  '--bpp', '4',
  '--size', '34',
  '--font', font,
  '--symbols', symbols,
  '--format', 'lvgl',
  '--lv-font-name', 'ui_egg_name_34',
  '--force-fast-kern-format',
  '-o', out,
];

const r = spawnSync('node', args, { stdio: 'inherit' });
process.exit(r.status === null ? 1 : r.status);
