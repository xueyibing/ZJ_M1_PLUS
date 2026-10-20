// Generate ui_egg_desc_24 (口味中文描述).
const { spawnSync } = require('child_process');
const path = require('path');

const conv = require('./font_conv_path');
const out = path.resolve(__dirname, '..', 'font', 'ui_egg_desc_24.c');
const font = 'C:\\Windows\\Fonts\\simhei.ttf';
const symbols = '双面全熟蛋黄凝固紧实轻煎软糯流心嫩柔鲜单制亮';

const args = [
  conv,
  '--no-compress', '--no-prefilter',
  '--bpp', '4',
  '--size', '24',
  '--font', font,
  '-r', '0x20',
  '-r', '0xB7',
  '--symbols', symbols,
  '--format', 'lvgl',
  '--lv-font-name', 'ui_egg_desc_24',
  '--force-fast-kern-format',
  '-o', out,
];

const r = spawnSync('node', args, { stdio: 'inherit' });
process.exit(r.status === null ? 1 : r.status);
