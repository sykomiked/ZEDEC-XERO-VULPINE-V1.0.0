# Wave 7 task: Hearth (slot 92), Recipes and meal plans
Read ../APP_WAVE_SPEC.md first.
What it does: recipes and weekly plans in the Banker (`files.read`, `files.write`, `search.query`).
Logic (critical): "contains an excluded allergen" in eve + myra witnessed (myra looks ingredients up in the allergen index); scaled quantities (qty x num / den, reduced with `use number_classics 1`) and weekly nutrition totals exact in trebago; recipes in myra.
Allergen flags are advisory; the app cannot vouch for product labels.
needs: fs.read, fs.write, search.
Planned (no runtime yet): ALGOL 58, Simula 67, Pascal.
