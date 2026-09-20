# echansen.org

My personal site: a handful of hand-written HTML pages and one stylesheet,
published to GitHub Pages. No build step, no framework, no dependencies.

## Structure

```
├── .github/workflows/
│   └── pages.yml       # Publishes public/ to GitHub Pages on every push to main
└── public/             # Everything that gets served
    ├── index.html      # Landing page
    ├── resume.html     # Experience & skills
    ├── github.html     # Code & systems
    ├── style.css       # The whole design system, including the print stylesheet
    ├── favicon.svg     # Source for the site monogram
    ├── favicon.ico     # Multi-size legacy fallback
    ├── apple-touch-icon.png
    ├── CNAME           # Custom domain
    └── .nojekyll       # Keeps GitHub from running Jekyll over the files
```

## Local preview

No build, so any static server works:

```bash
python3 -m http.server 8080 -d public
```

Then open http://localhost:8080.

## Deploying

Push to `main`. The workflow uploads `public/` and deploys it. Pages must be
set to **Settings → Pages → Source = GitHub Actions**.

## The resume

`resume.html` is the only copy of the resume; a plain-text `resume.md` and a
LaTeX source both used to exist alongside it and had drifted apart from it.

Its `@media print` block reflows the page into a one-page letter PDF: it hides
the back-link, subtitle and footer, reveals a name/contact line that only makes
sense on paper, and tightens the type. **It fits on one page with roughly an
inch to spare, so a couple of new bullets are fine but a new section is not.**
Re-check after editing by printing to PDF (Cmd-P → Save as PDF) and confirming
it is still one page.
