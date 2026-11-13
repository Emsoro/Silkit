// SQL 文本工具：语句分割、光标定位语句、轻量格式化。
// 与后端 db/sql_splitter 的语义保持一致（引号/注释感知），供执行范围与 DDL 提示使用。
(function () {
    'use strict';

    // ---------------------------------------------------------- 语句分割
    // 返回 [{start, end, sql}]，start/end 为原文偏移（含结尾分号）。
    function splitStatements(sql) {
        const text = String(sql || '');
        const out = [];
        let i = 0;
        let start = 0;
        let hasContent = false;

        while (i < text.length) {
            const ch = text[i];

            // 行注释
            if (ch === '-' && text[i + 1] === '-') {
                const nl = text.indexOf('\n', i);
                i = nl === -1 ? text.length : nl + 1;
                continue;
            }
            // 块注释
            if (ch === '/' && text[i + 1] === '*') {
                const close = text.indexOf('*/', i + 2);
                i = close === -1 ? text.length : close + 2;
                continue;
            }
            // 引号（'...' "..." `...`，支持 '' 与 "" 转义）
            if (ch === '\'' || ch === '"' || ch === '`') {
                const quote = ch;
                i += 1;
                while (i < text.length) {
                    if (text[i] === '\\' && quote !== '`') { i += 2; continue; }
                    if (text[i] === quote) {
                        if (text[i + 1] === quote) { i += 2; continue; }
                        i += 1;
                        break;
                    }
                    i += 1;
                }
                hasContent = true;
                continue;
            }

            if (!/\s/.test(ch)) hasContent = true;

            if (ch === ';') {
                const statement = text.slice(start, i + 1);
                if (hasContent) out.push({ start: start, end: i + 1, sql: statement });
                start = i + 1;
                hasContent = false;
                i += 1;
                continue;
            }
            i += 1;
        }

        if (hasContent) {
            out.push({ start: start, end: text.length, sql: text.slice(start) });
        }
        return out;
    }

    // 光标偏移所在语句；空隙处取下一条语句，末尾取最后一条。
    function statementAt(sql, offset) {
        const statements = splitStatements(sql);
        if (!statements.length) return null;
        const target = Math.max(0, Math.min(sql.length, Number(offset) || 0));
        for (let i = 0; i < statements.length; i += 1) {
            const item = statements[i];
            if (target >= item.start && target < item.end) return item;
            if (target < item.start) return item;
        }
        return statements[statements.length - 1];
    }

    // ---------------------------------------------------------- 轻量格式化
    // 目标：可读而非完美 —— 主子句换行、括号内容缩进、列表逗号换行、保留字大写。
    const MAJOR_SET = new Set(['SELECT', 'FROM', 'WHERE', 'GROUP BY', 'HAVING',
        'ORDER BY', 'LIMIT', 'OFFSET', 'UNION ALL', 'UNION', 'VALUES', 'SET',
        'INSERT INTO', 'UPDATE', 'DELETE FROM', 'LEFT JOIN', 'RIGHT JOIN',
        'INNER JOIN', 'FULL JOIN', 'CROSS JOIN', 'JOIN', 'OUTER JOIN',
        'RETURNING', 'ON CONFLICT']);
    const UPPERCASE_WORDS = new Set(['SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'NOT',
        'NULL', 'IS', 'IN', 'LIKE', 'BETWEEN', 'EXISTS', 'AS', 'DISTINCT', 'CASE',
        'WHEN', 'THEN', 'ELSE', 'END', 'ASC', 'DESC', 'BY', 'ON', 'INTO', 'VALUES',
        'SET', 'UPDATE', 'DELETE', 'INSERT', 'UNION', 'ALL', 'GROUP', 'ORDER',
        'HAVING', 'LIMIT', 'OFFSET', 'JOIN', 'LEFT', 'RIGHT', 'INNER', 'OUTER',
        'CROSS', 'FULL', 'USING', 'TRUE', 'FALSE']);

    function isIdentifierChar(ch) {
        return /[A-Za-z0-9_$]/.test(ch);
    }

    // 词法：comment / literal / word / punct（多字符运算符合并）。
    function tokenize(sql) {
        const text = String(sql || '');
        const tokens = [];
        let i = 0;

        while (i < text.length) {
            const ch = text[i];

            if (/\s/.test(ch)) { i += 1; continue; }

            if (ch === '-' && text[i + 1] === '-') {
                const nl = text.indexOf('\n', i);
                const end = nl === -1 ? text.length : nl;
                tokens.push({ type: 'comment', text: text.slice(i, end) });
                i = end;
                continue;
            }
            if (ch === '/' && text[i + 1] === '*') {
                const close = text.indexOf('*/', i + 2);
                const end = close === -1 ? text.length : close + 2;
                tokens.push({ type: 'comment', text: text.slice(i, end) });
                i = end;
                continue;
            }
            if (ch === '\'' || ch === '"' || ch === '`') {
                const quote = ch;
                let j = i + 1;
                while (j < text.length) {
                    if (text[j] === '\\' && quote !== '`') { j += 2; continue; }
                    if (text[j] === quote) {
                        if (text[j + 1] === quote) { j += 2; continue; }
                        j += 1;
                        break;
                    }
                    j += 1;
                }
                tokens.push({ type: 'literal', text: text.slice(i, j) });
                i = j;
                continue;
            }

            if (isIdentifierChar(ch)) {
                let j = i;
                while (j < text.length && (isIdentifierChar(text[j]) || text[j] === '.')) j += 1;
                tokens.push({ type: 'word', text: text.slice(i, j) });
                i = j;
                continue;
            }

            if ('()[],;.%'.includes(ch)) {
                tokens.push({ type: 'punct', text: ch });
                i += 1;
                continue;
            }

            if ('+-*=<>!|/'.includes(ch)) {
                let j = i + 1;
                while (j < text.length && '<>=!|+-*'.includes(text[j])) j += 1;
                tokens.push({ type: 'punct', text: text.slice(i, j) });
                i = j;
                continue;
            }

            tokens.push({ type: 'punct', text: ch });
            i += 1;
        }
        return tokens;
    }

    // 识别从 index 开始的主子句短语（跳过注释），返回 {phrase, end} 或 null。
    function majorPhraseAt(tokens, index) {
        let cursor = index;
        while (cursor < tokens.length && tokens[cursor].type === 'comment') cursor += 1;
        if (cursor >= tokens.length || tokens[cursor].type !== 'word') return null;
        const first = tokens[cursor].text.toUpperCase();
        // 单词短语
        if (['SELECT', 'FROM', 'WHERE', 'HAVING', 'LIMIT', 'OFFSET', 'VALUES',
            'SET', 'UPDATE', 'UNION'].indexOf(first) >= 0) {
            let end = cursor + 1;
            if (first === 'UNION' && tokens[end] && tokens[end].type === 'word' &&
                tokens[end].text.toUpperCase() === 'ALL') end += 1;
            return { phrase: first === 'UNION' && end > cursor + 1 ? 'UNION ALL' : first, end: end };
        }
        if (first === 'INSERT') {
            const next = tokens[cursor + 1];
            if (next && next.type === 'word' && next.text.toUpperCase() === 'INTO') {
                return { phrase: 'INSERT INTO', end: cursor + 2 };
            }
            return { phrase: 'INSERT INTO', end: cursor + 1 };
        }
        if (first === 'DELETE') {
            const next = tokens[cursor + 1];
            if (next && next.type === 'word' && next.text.toUpperCase() === 'FROM') {
                return { phrase: 'DELETE FROM', end: cursor + 2 };
            }
        }
        if (first === 'GROUP' || first === 'ORDER') {
            const next = tokens[cursor + 1];
            if (next && next.type === 'word' && next.text.toUpperCase() === 'BY') {
                return { phrase: first + ' BY', end: cursor + 2 };
            }
        }
        if (['LEFT', 'RIGHT', 'INNER', 'OUTER', 'FULL', 'CROSS'].indexOf(first) >= 0) {
            let end = cursor + 1;
            let phrase = first;
            // LEFT [OUTER] JOIN / INNER JOIN
            while (end < tokens.length && tokens[end].type === 'word') {
                const w = tokens[end].text.toUpperCase();
                if (w === 'OUTER' || w === 'JOIN') {
                    phrase += ' ' + w;
                    end += 1;
                    if (w === 'JOIN') break;
                    continue;
                }
                break;
            }
            if (phrase.endsWith('JOIN')) return { phrase: phrase, end: end };
        }
        if (first === 'JOIN') return { phrase: 'JOIN', end: cursor + 1 };
        if (first === 'ON' && tokens[cursor + 1] && tokens[cursor + 1].type === 'word' &&
            tokens[cursor + 1].text.toUpperCase() === 'CONFLICT') {
            return { phrase: 'ON CONFLICT', end: cursor + 2 };
        }
        return null;
    }

    // 预计算每个 '(' 的匹配 ')' 下标；-1 表示不配对。
    function matchParens(tokens) {
        const match = new Array(tokens.length).fill(-1);
        const stack = [];
        for (let i = 0; i < tokens.length; i += 1) {
            const t = tokens[i];
            if (t.type === 'punct' && t.text === '(') stack.push(i);
            else if (t.type === 'punct' && t.text === ')' && stack.length) {
                const open = stack.pop();
                match[open] = i;
                match[i] = open;
            }
        }
        return match;
    }

    function formatSql(sql) {
        const tokens = tokenize(sql);
        if (!tokens.length) return String(sql || '');
        const match = matchParens(tokens);

        const lines = [];
        let current = '';
        let indent = 0;
        let atLineStart = true;
        let needSpace = false;
        // 单行括号区间栈：区间内的逗号/主子句不换行。
        const inlineCloses = [];
        const inInline = function (i) {
            return inlineCloses.length && i < inlineCloses[inlineCloses.length - 1];
        };

        function flush() {
            lines.push(current.replace(/[ \t]+$/, ''));
            current = '';
            atLineStart = true;
            needSpace = false;
        }

        function emit(text, noSpaceBefore) {
            if (atLineStart) {
                current += '  '.repeat(indent);
                atLineStart = false;
            } else if (needSpace && !noSpaceBefore) {
                current += ' ';
            }
            current += text;
        }

        function newline() {
            if (!atLineStart) flush();
        }

        for (let i = 0; i < tokens.length; i += 1) {
            const t = tokens[i];
            const prev = tokens[i - 1];

            if (t.type === 'comment') {
                newline();
                emit(t.text, true);
                flush();
                continue;
            }

            if (t.type === 'punct' && t.text === ';') {
                emit(';', true);
                flush();
                continue;
            }

            if (t.type === 'punct' && t.text === '(') {
                const prevWord = prev && prev.type === 'word' ? prev.text.toUpperCase() : '';
                const isCall = !!prevWord && !UPPERCASE_WORDS.has(prevWord) && !/^JOIN$|^UNION/.test(prevWord);
                emit('(', isCall);           // 函数调用紧凑，子查询前留空格
                const close = match[i];
                if (close > 0) {
                    let span = 0;
                    let depth1Comma = false;
                    for (let k = i + 1; k < close; k += 1) {
                        span += tokens[k].text.length;
                        if (tokens[k].type === 'punct' && tokens[k].text === ',' && match[k] < 0) depth1Comma = true;
                    }
                    if (span <= 48 && !depth1Comma) {
                        inlineCloses.push(close);
                        needSpace = false;   // 单行内容紧凑
                        continue;
                    }
                }
                indent += 1;
                flush();
                continue;
            }

            if (t.type === 'punct' && t.text === ')') {
                if (inlineCloses.length && i === inlineCloses[inlineCloses.length - 1]) {
                    inlineCloses.pop();
                    emit(')', true);
                    needSpace = true;
                } else if (match[i] >= 0 && match[match[i]] === i) {
                    const open = match[i];
                    let span = 0;
                    let depth1Comma = false;
                    for (let k = open + 1; k < i; k += 1) {
                        span += tokens[k].text.length;
                        if (tokens[k].type === 'punct' && tokens[k].text === ',' && match[k] < 0) depth1Comma = true;
                    }
                    indent = Math.max(0, indent - 1);
                    if (span > 48 || depth1Comma) newline();
                    emit(')', true);
                    needSpace = true;
                } else {
                    emit(')', true);
                    needSpace = true;
                }
                continue;
            }

            if (t.type === 'punct' && t.text === ',') {
                emit(',', true);
                if (!inInline(i)) flush();
                else needSpace = false;
                continue;
            }

            if (t.type === 'punct' && t.text === '.') {
                emit('.', true);
                needSpace = false;
                continue;
            }

            if (t.type === 'punct') {
                // 运算符：两侧留空格（含 *、=、>、<、+、-、|、!）
                emit(t.text);
                needSpace = true;
                continue;
            }

            if (t.type === 'word') {
                const phrase = majorPhraseAt(tokens, i);
                if (phrase && !inInline(i)) {
                    newline();               // 主子句前换行
                    emit(phrase.phrase);
                    needSpace = true;        // 子句内容跟在同一行
                    i = phrase.end - 1;
                    continue;
                }
                emit(UPPERCASE_WORDS.has(t.text.toUpperCase()) ? t.text.toUpperCase() : t.text);
                needSpace = true;
                continue;
            }

            if (t.type === 'literal') {
                emit(t.text, true);
                needSpace = true;
                continue;
            }
        }
        if (!atLineStart) flush();

        return lines.join('\n').replace(/\n{3,}/g, '\n\n').trim();
    }

    window.SilkitSqlTools = {
        splitStatements: splitStatements,
        statementAt: statementAt,
        formatSql: formatSql
    };
})();
