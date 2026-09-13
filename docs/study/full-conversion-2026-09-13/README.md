# Étude « conversion totale WAD / Build » — rapports bruts (2026-09-13)

Synthèse : `docs/FULL_CONVERSION_PLAN.md`. Ces douze fichiers sont les rapports d'agents dont elle est
tirée ; ils contiennent des affirmations que les juges (J1-J3) ont **réfutées** — lire la synthèse
d'abord, ces rapports ensuite comme matériau, jamais comme source de vérité seule.

| fichier | rôle |
|---|---|
| R1_doom_core.md | inventaire du Doom non-renderer (`Mimas/core`), sous-système par sous-système, ce que le playsim exporte/importe |
| R2_engine.md | le moteur SlaveDriver comme hôte : boucle, sprites, 2D, son, sauvegarde, mémoire, entrée |
| R3_pipeline.md | état des convertisseurs doom2ps/duke2ps, formats des morceaux manquants, disque, capacités |
| R4_duke.md | couplage jeu Duke ↔ moteur Build (1 302 appels), clean-room, CON, RAM, licences |
| R5_memory.md | le mur mémoire : .map du fork, code Doom, structures de carte, .LEV, plans A/B/C, VDP2, sauvegarde |
| R6_mimas.md | Mimas comme donneur : couche plateforme brique par brique, fonctions à fournir, pièges |
| D1_fidelite_doom.md | proposition « fidélité Doom d'abord » |
| D2_pipeline_disque.md | proposition « pipeline et disque d'abord » |
| D3_duke_risques.md | proposition « Duke et risques d'abord » |
| J1_exactitude.md | juge : exactitude contre le code (18 réfutations, 12 manques) |
| J2_completude.md | juge : complétude contre « TOUT » (listes de contrôle Doom 32 / Duke 11) |
| J3_faisabilite.md | juge : faisabilité et économie (arithmétique RAM/CPU/effort refaite) |

Scripts de mesure : `tools/study/fullconv/`. Les CON de Duke extraits pour la mesure ne sont **pas**
dans le dépôt (3D Realms).

## Révision v2 (2026-09-14) — après les réserves owner

`FULL_CONVERSION_PLAN_v1.md` est la synthèse du 13 archivée telle quelle (architecture C, gouverneurs,
2D sur NBG0, son Mimas, RAM « B non acquis ») ; la version courante est `docs/FULL_CONVERSION_PLAN.md`
(architecture D : un moteur, des jeux en données + verbes). Quatre lectures supplémentaires du moteur :

| fichier | rôle |
|---|---|
| R7_engine_actors.md | acteurs/monstres/IA : bus de messages, 17 monstres codés en dur, briques AICOMMON génériques, services (projectiles, hitscan, LOS, radius, son) ; estimation « états Doom sur ce moteur » |
| R8_engine_physics_world_save.md | physique (sphère, friction/gravité par sprite, marche 32 u), joueur 60 Hz, armes, push blocks (dy seulement), déclencheurs, lumière, SaveRec 100 o, cadences ; PLRCYL.C non commité = modèle cylindre Doom |
| R9_engine_2d_vdp2.md | menus/HUD/texte/arme/cartes tout VDP1 ; VRAM VDP1 256 o libres en jeu ; feuille NBG0 B0+B1 (grenade/manacle) ; évaluation « 2D du moteur avec les données Doom » vs « feuille NBG1 pour la 2D Doom inchangée » |
| R10_frame_cost_decomposition.md | boucle de niveau étage par étage, esclave, profileur FRT existant (actif dans tous les builds), 586 asserts, 10 timers d'étage proposés avec valeurs attendues |
