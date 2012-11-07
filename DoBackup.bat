cd ..
del TTTTE.zip
rmdir /S /Q TTTTE
mkdir TTTTE
xcopy Electronics\*.* TTTTE\Electronics\ /S /E
cd TTTTE
cd Electronics
call CleanProjectFullyWithAttrib.bat
