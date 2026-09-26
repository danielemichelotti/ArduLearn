@echo off
rem Crea l'eseguibile "ArduLearn.exe" nella cartella dist\
rem Requisiti: Python 3 con pyserial e PyInstaller
rem   python -m pip install --user pyserial pyinstaller
cd /d "%~dp0"

if not exist "ardulearn.ico" (
  echo Manca ardulearn.ico: crealo con  python make_icon.py
  exit /b 1
)
for %%F in (avrdude.exe avrdude.conf bossac.exe) do (
  if not exist "bin\%%F" (
    echo Manca bin\%%F
    exit /b 1
  )
)
if not exist "sd\PLC\INDEX.GZ" echo Attenzione: manca sd\PLC\INDEX.GZ ^(pagina per la microSD^)
if exist "firmware\PlcBlocchi_r4minima.bin" echo Nota: firmware\PlcBlocchi_r4minima.bin non serve piu' ^(UNO R4 Minima non supportata^): si puo' cancellare

python -m PyInstaller --noconfirm --clean --onefile --windowed --name "ArduLearn" ^
  --icon "%~dp0ardulearn.ico" ^
  --add-data "%~dp0ardulearn.ico;." ^
  --add-data "%~dp0ardulearn.png;." ^
  --add-data "%~dp0ardulearn_32.png;." ^
  --distpath dist --workpath build --specpath build ^
  --add-data "%~dp0bin;bin" ^
  --add-data "%~dp0firmware;firmware" ^
  --add-data "%~dp0sd;sd" ^
  --exclude-module unittest --exclude-module pydoc --exclude-module doctest ^
  --exclude-module pdb --exclude-module lib2to3 --exclude-module setuptools ^
  --exclude-module pip --exclude-module test --exclude-module sqlite3 ^
  --exclude-module asyncio --exclude-module multiprocessing ^
  ardulearn_app.py
if errorlevel 1 (
  echo Creazione dell'eseguibile non riuscita.
  exit /b 1
)
echo.
echo Fatto: dist\ArduLearn.exe
