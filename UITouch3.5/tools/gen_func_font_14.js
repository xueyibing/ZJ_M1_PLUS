// Regenerate ui_func_font_14 to cover the new page title "口味选择".
// Uses spawnSync with array args so Chinese characters are passed as UTF-8
// (PowerShell would otherwise mangle the argv encoding).
const { spawnSync } = require('child_process');

const conv = 'C:\\Users\\93160\\AppData\\Roaming\\npm\\node_modules\\lv_font_conv\\lv_font_conv.js';
const out = 'D:\\gitee\\ZJ_M1\\UITouch3.5\\font\\ui_func_font_14.c';
const font = 'C:\\Windows\\Fonts\\simhei.ttf';

// 主页顶部标题“口味选择”，另保留旧功能页字形以防其它处引用。
const symbols = '口味选择切换双面煎蛋单太阳煮';

const args = [
  conv,
  '--no-compress', '--no-prefilter',
  '--bpp', '4',
  '--size', '20',
  '--font', font,
  '-r', '0x20-0x7F',
  '--symbols', symbols,
  '--format', 'lvgl',
  '--lv-font-name', 'ui_func_font_14',
  '--force-fast-kern-format',
  '-o', out,
];

const r = spawnSync('node', args, { stdio: 'inherit' });
process.exit(r.status === null ? 1 : r.status);
