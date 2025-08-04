#!/bin/bash

echo "Installing cx.."

# Paso 1: Descargar el código fuente
echo "Downloading cx source.."
curl -s -L https://raw.githubusercontent.com/zer0users/cx/main/cx_build.c -o cx_build.c
echo "Done! Thank Jehovah!"

# Paso 2: Compilar
echo "Building source.."
gcc -o cx cx_build.c -lz &> /dev/null
echo "Done! Thank Jehovah"

# Paso 3: Instalar el ejecutable en /usr/bin
echo "Moving builded cx to /usr/bin/.."
sudo mv cx /usr/bin/cx &> /dev/null
sudo chmod +x /usr/bin/cx
echo "✓ CX installed at /usr/bin/cx"

# Paso 4: Crear .desktop para CX
echo "Registering CX file launcher..."
mkdir -p ~/.local/share/applications
cat <<EOF > ~/.local/share/applications/cx.desktop
[Desktop Entry]
Name=CX (.cxA)
Exec=cx %f
Terminal=false
Type=Application
MimeType=application/x-cxa
Categories=Utility;
Icon=text-x-csrc
EOF

# Paso 5: Crear tipo MIME para .cxA
mkdir -p ~/.local/share/mime/packages
cat <<EOF > ~/.local/share/mime/packages/cx-mime.xml
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-cxa">
    <comment>CX Application</comment>
    <glob pattern="*.cxA"/>
    <icon name="text-x-csrc"/>
  </mime-type>
</mime-info>
EOF

# Paso 6: Actualizar bases de datos de MIME y .desktop
update-mime-database ~/.local/share/mime &> /dev/null
update-desktop-database ~/.local/share/applications &> /dev/null

# Paso 7: Asociar CX a .cxA
xdg-mime default cx.desktop application/x-cxa

# Paso 8: Mensaje final
echo "✓ CX is now integrated!"
echo "You can now double click any .cxA file to run it!"
echo "Done! Thank Jehovah 🙏"
