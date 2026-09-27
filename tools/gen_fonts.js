/*
 * 从 charset.txt 去重取字符，调用本地安装的 lv_font_conv 生成
 * 12px / 16px 中文点阵字体子集 C 文件到 ../main/fonts/。
 * 用法：cd tools && npm install && node gen_fonts.js
 */
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const pjPath = require.resolve('lv_font_conv/package.json');
const pj = require(pjPath);
const binRel = typeof pj.bin === 'string' ? pj.bin : pj.bin['lv_font_conv'];
const binPath = path.join(path.dirname(pjPath), binRel);

const raw = fs.readFileSync(path.join(__dirname, 'charset.txt'), 'utf8');
const chars = [...new Set(raw.replace(/\s+/g, ''))].join('');
console.log(`lv_font_conv: ${binPath}`);
console.log(`unique symbols: ${chars.length}`);

const fontFile = process.env.METRONOME_FONT_FILE || 'C:\\Windows\\Fonts\\simhei.ttf';
if (!fs.existsSync(fontFile)) {
    console.error(`font file not found: ${fontFile}`);
    process.exit(1);
}

for (const size of [12, 16]) {
    const out = path.join(__dirname, '..', 'main', 'fonts', `ui_font_cn${size}.c`);
    fs.mkdirSync(path.dirname(out), { recursive: true });
    const args = [
        binPath,
        '--no-compress', '--no-prefilter',
        '--bpp', '4',
        '--size', String(size),
        '--font', fontFile,
        '-r', '0x20-0x7E',
        '--symbols', chars,
        // simhei 缺失的符号从 Segoe UI Symbol 合并
        '--font', 'C:\\Windows\\Fonts\\seguisym.ttf',
        '--symbols', '◀▶♩♪',
        '--format', 'lvgl',
        '-o', out,
        '--force-fast-kern-format',
    ];
    execFileSync(process.execPath, args, { stdio: 'inherit' });
    console.log(`generated: ${out}`);
}
