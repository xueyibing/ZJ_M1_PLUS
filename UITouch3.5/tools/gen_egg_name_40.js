// Generate ui_egg_name_40 (口味大字名称: 全熟/糖心/软嫩/太阳蛋).
const { spawnSync } = require('child_process');
const path = require('path');

const conv = require('./font_conv_path');
const out = path.resolve(__dirname, '..', 'font', 'ui_egg_name_40.c');
const font = 'C:\\Windows\\Fonts\\simhei.ttf';
const symbols = '全熟糖心软嫩太阳蛋';

const args = [
  conv,
  '--no-compress', '--no-prefilter',
  '--bpp', '4',
  '--size', '40',
  '--font', font,
  '--symbols', symbols,
  '--format', 'lvgl',
  '--lv-font-name', 'ui_egg_name_40',
  '--force-fast-kern-format',
  '-o', out,
];

const r = spawnSync('node', args, { stdio: 'inherit' });
process.exit(r.status === null ? 1 : r.status);
