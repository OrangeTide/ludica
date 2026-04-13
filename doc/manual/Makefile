MANUAL_SRC = manual.md
OUTDIR = ../../_site

MANUAL_HTML = $(OUTDIR)/manual.html
MANUAL_PDF = $(OUTDIR)/manual.pdf

all: html

html: $(MANUAL_HTML)
pdf: $(MANUAL_PDF)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(MANUAL_HTML): $(MANUAL_SRC) style.css | $(OUTDIR)
	pandoc $< \
		-f markdown -t html \
		--standalone \
		--metadata title="Game Engine Manual" \
		--css style.css \
		-o $@
	cp style.css $(OUTDIR)/

$(MANUAL_PDF): $(MANUAL_SRC) | $(OUTDIR)
	pandoc $< -o $@

clean:
	rm -f $(MANUAL_HTML) $(MANUAL_PDF) $(OUTDIR)/style.css

.PHONY: all html pdf clean
