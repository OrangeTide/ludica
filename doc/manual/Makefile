MANUAL_SRC = manual.md
MANUAL_HTML = manual.html
MANUAL_PDF = manual.pdf

all: html

html: $(MANUAL_HTML)
pdf: $(MANUAL_PDF)

$(MANUAL_HTML): $(MANUAL_SRC) style.css
	pandoc $< \
		-f markdown -t html \
		--standalone \
		--metadata title="Game Engine Manual" \
		--css style.css \
		-o $@

$(MANUAL_PDF): $(MANUAL_SRC)
	pandoc $< -o $@

clean:
	rm -f $(MANUAL_HTML) $(MANUAL_PDF)

.PHONY: all html pdf clean
