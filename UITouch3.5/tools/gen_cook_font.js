// Generate ui_cook_font_20 for the cooking overlay and reservation button.
// Uses spawnSync with array args so Chinese characters are passed as UTF-8
// (PowerShell would otherwise mangle the argv encoding).
const { spawnSync } = require('child_process');
const path = require('path');

const conv = require('./font_conv_path');
const out = path.resolve(__dirname, '..', 'font', 'ui_cook_font_20.c');
const font = 'C:\\Windows\\Fonts\\simhei.ttf';

// Keep every Chinese glyph rendered with ui_cook_font_20 here. This includes
// the cooking title, normal/sunny-side-up remaining text, action buttons and
// the compact reservation label. Keeping the generator in sync prevents a
// valid UTF-8 string from appearing as a missing-glyph square on the device.
const symbols = '正在制作煎制流心溏心糖心软嫩软黄全熟太阳蛋内预约调整到指定时间点确定取消枚紧急停止后续当前单面进度等待计时剩余';

const args = [
  conv,
  '--no-compress', '--no-prefilter',
  '--bpp', '4',
  '--size', '20',
  '--font', font,
  '--symbols', symbols,
  '-r', '0x20',      // space
  '-r', '0x25',      // %
  '-r', '0x30-0x39', // digits
  '-r', '0x3A',      // colon :
  '-r', '0xB7',      // middle dot ·
  '-r', '0xD7',      // multiply sign ×
  '--format', 'lvgl',
  '--lv-font-name', 'ui_cook_font_20',
  '--force-fast-kern-format',
  '-o', out,
];

const r = spawnSync('node', args, { stdio: 'inherit' });
process.exit(r.status === null ? 1 : r.status);
