# Evidence outside the source diff

Generated reports and historical file inventories are attachments, not test code.
[The verified report bundle](https://gitlab.espressif.cn:6688/-/project/274/uploads/3d4d3f108b6592ebdddc269ffa0ab5b4/present-five-board-results-20261111.tar.gz) includes the final webpage, full JSON/CSV,
memory and address details, the previous detailed MR text and `history/archive-index.json`.
Its SHA-256 and reproduction commands are in [VALIDATION.md](VALIDATION.md).

The approximately 90 MB development snapshot remains in the independently
verified offline backup. GitLab rejected that upload with HTTP 413; a shared raw
snapshot URL is still pending. The attachment's file inventory is provenance,
not a claim that every original raw file is available for remote download.

Historical paths in measurements refer to that snapshot, not the current checkout.
SDK diagnostic extensions remain historical material, not a production dependency.
