# release
make RELEASE=1 CC=emcc CXX=em++ AR=emar 
# debug: make CC=emcc CXX=em++ AR=emar
./scripts/gen-pages.sh
python3 -m http.server -d _site 8080
