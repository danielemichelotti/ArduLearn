// Sito di ArduLearn. Si costruisce con "npm run build" (cartella _site/).
// I contenuti stanno in src/ e si modificano anche dal pannello /admin (Sveltia CMS).
import { HtmlBasePlugin } from "@11ty/eleventy";

export default function (eleventyConfig) {
  // prova online: la stessa pagina che gira sulle schede, in modalità simulatore
  eleventyConfig.addPassthroughCopy({ "../web/index.html": "prova/app/index.html", "../web/modules.js": "prova/app/modules.js" });
  // PDF scaricabili dal sito (gli stessi del repository)
  eleventyConfig.addPassthroughCopy({ "../docs/ArduLearn_brochure.pdf": "scarica/ArduLearn_brochure.pdf", "../docs/ArduLearn_manuale_docente.pdf": "scarica/ArduLearn_manuale_docente.pdf" });
  eleventyConfig.addPassthroughCopy({ "src/assets": "assets", "src/img": "img", "src/admin": "admin" });
  // manuale da leggere online: immagini e script dalla cartella del manuale
  for (const f of ["shots", "foto", "figs.js", "figdefs.js", "data.js", "paginate.js", "Caveat.ttf"]) eleventyConfig.addPassthroughCopy({ [`../docs/manuale/${f}`]: `manuale/leggi/${f}` });
  eleventyConfig.addPlugin(HtmlBasePlugin);

  // raccolte modificabili dal pannello
  eleventyConfig.addCollection("pagine", api => api.getFilteredByGlob("src/pagine/*.md").sort((a, b) => (a.data.ordine ?? 99) - (b.data.ordine ?? 99)));
  eleventyConfig.addCollection("novita", api => api.getFilteredByGlob("src/novita/*.md").sort((a, b) => b.date - a.date));
  eleventyConfig.addCollection("video", api => api.getFilteredByGlob("src/video/*.md").sort((a, b) => (a.data.ordine ?? 99) - (b.data.ordine ?? 99) || b.date - a.date));

  eleventyConfig.addFilter("head", (a, n) => (a || []).slice(0, n));
  eleventyConfig.addFilter("data_it", d => new Date(d).toLocaleDateString("it-IT", { day: "numeric", month: "long", year: "numeric" }));
  // codice del video YouTube da un link qualsiasi (watch?v=, youtu.be/, shorts/, embed/)
  eleventyConfig.addFilter("youtube_id", u => {
    const m = String(u || "").match(/(?:v=|youtu\.be\/|shorts\/|embed\/)([\w-]{11})/);
    return m ? m[1] : String(u || "").trim();
  });
  eleventyConfig.addFilter("categorie", list => [...new Set(list.map(v => v.data.categoria || "Altri video"))]);

  return {
    dir: { input: "src", includes: "_includes", data: "_data", output: "_site" },
    markdownTemplateEngine: "njk",
    htmlTemplateEngine: "njk",
    pathPrefix: process.env.SITO_BASE || "/",
  };
}
