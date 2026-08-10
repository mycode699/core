# CoC Office / 可圈办公 品牌规范

**English:** CoC Office · **code / domain style:** `cocoffice`  
**中文产品名：** 可圈办公  
**Bundle ID：** `com.cooffice.pc`  
**官网：** https://www.03122.com  
**Vendor：** 可圈办公  

## 用户可见命名

| 场景 | 文案 |
|------|------|
| 窗口 / 关于 / 安装包 | 可圈办公 |
| 英文副标 | CoC Office |
| 技术标识 / 日志 | cocoffice |
| AI / 会员 | 可圈 AI · api.03122.com |
| 资料盘 | 可圈资料盘 |

## 禁止出现在用户 UI

- LibreOffice / OpenOffice.org / The Document Foundation（产品名语境）
- libreoffice.org 更新 / 捐赠 / 扩展更新链接
- “衍生自 LibreOffice” 等溯源话术（关于框改为 CoC 自有叙事）
- LO 默认捐赠横幅话术

> 源码文件头 MPL / “This file is part of the LibreOffice project” **保留**（许可证要求，不对用户展示）。

## 配置要点

| 项 | 值 |
|----|-----|
| `--with-product-name` | 可圈办公 |
| `--with-vendor` | 可圈办公 |
| `--with-macosx-bundle-identifier` | com.cooffice.pc |
| `versionrc` UpdateURL / ExtensionUpdateURL | 空（不连 LO 更新） |
| InfoURL / ReleaseNotesURL | www.03122.com |
| ShowDonation | false |

## 用户配置目录

macOS: `~/Library/Application Support/可圈办公/`  
Windows: `%APPDATA%\可圈办公\`（以及 `%APPDATA%\kqoffice` 配置侧）  

## 检查清单（发版前）

```bash
# About / 选项树 / 安装器描述 不含 LibreOffice 产品名
bash downstream-branding/bin/kq-brand-smoke.sh
# 或
grep -R "LibreOffice" instdir/*/Contents/Resources/versionrc || true
```
