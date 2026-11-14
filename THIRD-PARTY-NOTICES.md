# 第三方组件与许可声明

Silkit 自身以 MIT 许可证发布（见 [LICENSE](LICENSE)）。发行包内包含以下第三方组件，其版权归各自作者所有，并按下列许可证使用。

| 组件 | 版本 | 用途 | 许可证 | 获取源码 |
|---|---|---|---|---|
| TauriCPP | 内置副本 | Win32 + WebView2 应用框架 | MIT | 本仓库 `TauriCPP/`（见 `TauriCPP/LICENSE`） |
| nlohmann/json | 3.11.3 | JSON 解析与序列化 | MIT | https://github.com/nlohmann/json |
| WebView2 SDK | vcpkg 当前版本 | WebView2 头文件与加载器 | Microsoft 软件许可 | https://learn.microsoft.com/microsoft-edge/webview2/ |
| SQLite | vcpkg 当前版本 | 内置 SQLite 驱动 | Public Domain | https://sqlite.org/ |
| libmariadb | vcpkg 当前版本 | MySQL / MariaDB 驱动 | LGPL-2.1 | https://github.com/mariadb-corporation/mariadb-connector-c |
| libpq | vcpkg 当前版本 | PostgreSQL 驱动 | PostgreSQL License | https://www.postgresql.org/ |
| CodeMirror | 5.x | SQL 编辑器 | MIT | https://codemirror.net/5/ |

## MIT License

适用于 TauriCPP、nlohmann/json、CodeMirror：

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## PostgreSQL License

适用于 libpq：

```
Permission to use, copy, modify, and distribute this software and its
documentation for any purpose, without fee, and without a written agreement
is hereby granted, provided that the above copyright notice and this
paragraph and the following two paragraphs appear in all copies.

IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR DIRECT,
INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST
PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION.

THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT
NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS,
AND THE COPYRIGHT HOLDER HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT,
UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
```

## LGPL-2.1（libmariadb）

libmariadb 以 GNU Lesser General Public License 2.1 授权，Silkit 以静态链接方式使用。
完整许可证文本见 https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html 。

如需重新链接 libmariadb 的其他版本，可使用仓库内的 `build.ps1` 与 `vcpkg.json` 重新构建；构建脚本与全部 Silkit 源码在发布仓库中提供。

## WebView2 Runtime

Silkit 不随包分发 WebView2 运行时，使用系统已安装的 Microsoft Edge WebView2 Runtime（Windows 11 自带）。
运行时缺失时应用会显示中文安装引导。WebView2 SDK 的使用受 Microsoft 软件许可条款约束。

## SQLite

SQLite 已发布到公有领域（public domain），无附加许可要求。
