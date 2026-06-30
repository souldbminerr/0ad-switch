rem **Download from the latest nightly build:**
rem ** - translations**
rem ** - SPIR-V shaders**
rem ** - game built files for Windows**

rem **This will overwrite any uncommitted changes to:**
rem ** - messages.json in i18n folders**
rem ** - readme.txt and helper scripts in this directory**

where svn || (
  @echo.
  @echo The "svn" executable was not found in your PATH. Make sure you installed TortoiseSVN
  @echo and that you selected "command line client tools" during the installation.
  @echo.
  @pause
  @exit
)

set "repourl=https://svn.wildfiregames.com/nightly-build/trunk"

rem **Translations**
svn export --force --depth files %repourl%/binaries/data/l10n ..\data\l10n
for %%m in (mod public) do (
  svn export --force --depth files %repourl%/binaries/data/mods/%%m/l10n ..\data\mods\%%m\l10n
)
svn export --force %repourl%/binaries/data/mods/public/gui/credits/texts/translators.json ..\data\mods\public\gui\credits\texts\translators.json

rem **SPIR-V shaders**
for %%m in (mod public) do (
  svn export --force %repourl%/binaries/data/mods/%%m/shaders/spirv ..\data\mods\%%m\shaders\spirv
)

rem **Game built files**
svn export --force %repourl%/binaries/system .
