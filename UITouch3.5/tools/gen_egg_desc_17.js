// Generate ui_egg_desc_17 for English subtitles and Chinese descriptions.
// Uses spawnSync with array args so Chinese characters are passed as UTF-8.
const { spawnSync } = require('child_process');

const conv = 'C:\\Users\\93160\\AppData\\Roaming\\npm\\node_modules\\lv_font_conv\\lv_font_conv.js';
const out = 'D:\\gitee\\ZJ_M1\\UITouch3.5\\font\\ui_egg_desc_17.c';
const font = 'C:\\Windows\\Fonts\\simhei.ttf';

const symbols = '双面全熟蛋黄凝固紧实轻煎软糯流心嫩柔鲜单制亮';

const args = [
  conv,
  '--no-compress', '--no-prefilter',
  '--bpp', '4',
  '--size', '17',
  '--font', font,
  '-r', '0x20-0x7E',
  '-r', '0xB7',
  '--symbols', symbols,
  '--format', 'lvgl',
  '--lv-font-name', 'ui_egg_desc_17',
  '--force-fast-kern-format',
  '-o', out,
];

const r = spawnSync('node', args, { stdio: 'inherit' });
process.exit(r.status === null ? 1 : r.status);
