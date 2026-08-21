# Data files Blockworlds ships in data/.
#
# Included from CMakeLists.txt right before set_glob(DATA ...) so they end up in
# EXPECTED_DATA, which is what the glob checks its result against and what gets
# copied next to the built server.

list(APPEND EXPECTED_DATA
  chatfilter_words.txt
  maps/blmapV3ROYAL.map
  maps/store.map
)

# EXPECTED_DATA has to stay complete and sorted, which set_glob checks.
list(SORT EXPECTED_DATA)
