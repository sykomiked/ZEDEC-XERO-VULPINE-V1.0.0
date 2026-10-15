# Wave 8 task: Hearth (slot 92), Recipes and meal plans
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 92, build position 91 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: recipes and weekly plans in the Banker (`files.read`, `files.write`, `search.query`).
Logic (critical): "contains an excluded allergen" in eve + myra witnessed (myra looks ingredients up in the allergen index); scaled quantities (qty x num / den, reduced with `use number_classics 1`) and weekly nutrition totals exact in trebago; recipes in myra.
Allergen flags are advisory; the app cannot vouch for product labels.
needs: fs.read, fs.write, search.
Planned (no runtime yet): ALGOL 58, Simula 67, Pascal.
