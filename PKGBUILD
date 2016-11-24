pkgbase=gfx2gfx-pdftext
pkgname=$pkgbase-git
pkgver=0.9.2
pkgrel=5
pkgdesc="A fork of SWFTools' gfx2gfx, preserving text as text in PDFs"
arch=('i686' 'x86_64')
url="https://github.com/RunasSudo/gfx2gfx-pdftext"
license=('GPL')
depends=('giflib' 'freeglut' 'lame' 't1lib' 'libjpeg' 'fontconfig')
makedepends=('bison' 'flex' 'zlib' 'patch')
source=(git+https://github.com/RunasSudo/gfx2gfx-pdftext.git)
sha256sums=('SKIP')

prepare() {
  cd ${srcdir}/$pkgbase
}

build() {
  cd ${srcdir}/$pkgbase

  ./configure --prefix=/usr
  make
}

package() {
  cd ${srcdir}/$pkgbase

  mkdir -p ${pkgdir}/usr/bin
  install -c src/gfx2gfx ${pkgdir}/usr/bin/gfx2gfx
}
