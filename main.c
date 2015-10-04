#include <string.h>
#include <time.h>

#include <sphinxbase/err.h>
#include <sphinxbase/ckd_alloc.h>
#include <sphinxbase/ngram_model.h>

typedef struct array_heap_node_s {
	int32 key;
	void *value;
} array_heap_node_t;

typedef struct array_heap_s {
	uint32 size;
	uint32 capacity;
	array_heap_node_t *nodes;
} array_heap_t;

array_heap_t *array_heap_new(uint32 capacity) {
	array_heap_t *heap = ckd_malloc(sizeof(array_heap_t));
	heap->size = 0;
	heap->capacity = capacity;
	heap->nodes = ckd_calloc(capacity, sizeof(array_heap_node_t));
	return heap;
}

void array_heap_free(array_heap_t *heap) {
	ckd_free(heap->nodes);
	ckd_free(heap);
}

uint32 array_heap_parent(uint32 index) {
	return (index - 1) / 2;
}

uint32 array_heap_left_child(uint32 index) {
	return 2 * index + 1;
}

uint32 array_heap_right_child(uint32 index) {
	return 2 * index + 2;
}

int array_heap_full(array_heap_t *heap) {
	return heap->capacity <= heap->size;
}

void array_heap_add(array_heap_t *heap, int32 key, void *value) {
	uint32 current;

	if (array_heap_full(heap)) {
		E_ERROR("Trying to add element to a full heap");
		return;
	}

	current = heap->size;
	heap->size++;
	while (current != 0 && heap->nodes[array_heap_parent(current)].key > heap->nodes[current].key) {
		heap->nodes[current] = heap->nodes[array_heap_parent(current)];
		current = array_heap_parent(current);
	}
	heap->nodes[current].key = key;
	heap->nodes[current].value = value;
}

void *array_heap_element(array_heap_t *heap, uint32 index) {
	return heap->nodes[index].value;
}

int32 array_heap_min_key(array_heap_t *heap) {
	if (heap->size == 0) {
		E_ERROR("Trying to get minimum element from empty heap");
	}
	return heap->nodes[0].key;
}

void *array_heap_pop(array_heap_t *heap) {
	void *result;
	int32 current, min;
	array_heap_node_t temp;

	if (heap->size == 0) {
		E_ERROR("Trying to get minimum element from empty heap");
	}
	result = heap->nodes[0].value;
	heap->nodes[0] = heap->nodes[heap->size - 1];
	heap->size--;
	current = 0;
	while (TRUE) {
		min = current;
		if (array_heap_left_child(current) < heap->size && heap->nodes[array_heap_left_child(current)].key
				< heap->nodes[min].key) {
			min = array_heap_left_child(current);
		}
		if (array_heap_right_child(current) < heap->size && heap->nodes[array_heap_right_child(current)].key
				< heap->nodes[min].key) {
			min = array_heap_right_child(current);
		}
		if (min == current) {
			return result;
		}
		temp = heap->nodes[min];
		heap->nodes[min] = heap->nodes[current];
		heap->nodes[current] = temp;
		current = min;
	}
}

typedef struct tree_element_s {
	int32 wid;
	int32 probability;
	struct tree_element_s *parent;
} tree_element_t;

tree_element_t *tree_element_new(int32 wid, int32 probability, tree_element_t* parent) {
	tree_element_t *tree_element = ckd_malloc(sizeof(tree_element_t));
	tree_element->wid = wid;
	tree_element->parent = parent;
	tree_element->probability = probability;
	return tree_element;
}

int graphemes_fit_count(const char *word, uint32 offset, const char* unigram_text) {
	int32 count = 0;

	word += offset;
	while (*word && *unigram_text && *unigram_text != '<' && *unigram_text != '}') {
		if (*unigram_text == '|') {
			unigram_text++;
		}
		if (*word != *unigram_text) {
			return 0;
		}
		count++;
		unigram_text++;
		word++;
	}
	return count;
}

char *unwind_phoneme(ngram_model_t *model, tree_element_t *tree_element) {
	int32 i, j, size = 0;
	char* phoneme;
	const char* unigram_phoneme;
	tree_element_t *element = tree_element;

	while (element) {
		unigram_phoneme = strstr(ngram_word(model, element->wid), "}") + 1;
		if (strcmp(unigram_phoneme, "_") != 0) {
			size += strlen(unigram_phoneme) + 1;
		}
		element = element->parent;
	}

	phoneme = ckd_malloc(size);
	phoneme[size - 1] = '\0';
	i = size - 2;

	element = tree_element;
	while (element) {
		unigram_phoneme = strstr(ngram_word(model, element->wid), "}") + 1;
		if (strcmp(unigram_phoneme, "_") != 0) {
			i -= strlen(unigram_phoneme);
			j = i + 1;
			while (*unigram_phoneme) {
				phoneme[j] = *unigram_phoneme == '|' ? ' ' : *unigram_phoneme;
				j++;
				unigram_phoneme++;
			}
			if (i >= 0) {
				phoneme[i] = ' ';
			}
			i--;
		}
		element = element->parent;
	}
	return phoneme;
}

void try_add_tree_element(ngram_model_t *model, int32 wid, int32 *history, int32 history_size,
		tree_element_t *tree_element_from, array_heap_t *heap) {
	int32 nused;
	int32 probability = ngram_ng_prob(model, wid, history, history_size, &nused);
	if (tree_element_from) {
		probability += tree_element_from->probability;
	}
	if (!array_heap_full(heap)) {
		array_heap_add(heap, probability, tree_element_new(wid, probability, tree_element_from));
	} else if (array_heap_min_key(heap) < probability) {
		ckd_free(array_heap_pop(heap));
		array_heap_add(heap, probability, tree_element_new(wid, probability, tree_element_from));
	}
}

int32 unwind_history(int32 *history, tree_element_t *tree_element, int32 start_wid) {
	int32 i = 0;

	while (tree_element) {
		history[i] = tree_element->wid;
		tree_element = tree_element->parent;
		i++;
	}
	history[i] = start_wid;
	return i + 1;
}

void try_add_tree_elements(ngram_model_t *model, int32 wid, array_heap_t *previous, array_heap_t *heap_to_fill,
		int32 *history_buffer, int32 start_wid) {
	int32 i, history_size;

	if (previous == NULL) {
		history_buffer[0] = start_wid;
		try_add_tree_element(model, wid, history_buffer, 1, NULL, heap_to_fill);
	} else {
		for (i = 0; i < previous->size; i++) {
			tree_element_t *tree_element = array_heap_element(previous, i);
			history_size = unwind_history(history_buffer, tree_element, start_wid);
			try_add_tree_element(model, wid, history_buffer, history_size, tree_element, heap_to_fill);
		}
	}
}

char *g2p(ngram_model_t *model, char *grapheme, uint32 level_count_limit) {
	int32 i, j, n, wid, fit_count;
	array_heap_t **tree_table;
	const char* unigram_text;
	char* phoneme;
	int32 *history_buffer;
	int32 start_wid, end_wid;
	const uint32 total_unigrams = *ngram_model_get_counts(model);

	n = strlen(grapheme);
	tree_table = ckd_calloc(n + 1, sizeof(array_heap_t *));
	for (i = 0; i < n; i++) {
		tree_table[i] = array_heap_new(level_count_limit);
	}
	tree_table[n] = array_heap_new(1);
	history_buffer = ckd_calloc(n + 1, sizeof(int32));
	start_wid = ngram_wid(model, "<s>");
	end_wid = ngram_wid(model, "</s>");

	for (i = 0; i < n; i++) {
		for (wid = 0; wid < total_unigrams; wid++) {
			unigram_text = ngram_word(model, wid);
			fit_count = graphemes_fit_count(grapheme, i, unigram_text);
			if (fit_count != 0) {
				try_add_tree_elements(model, wid, i == 0 ? NULL : tree_table[i - 1], tree_table[i + fit_count - 1],
						history_buffer, start_wid);
			}
		}

	}

	try_add_tree_elements(model, end_wid, tree_table[n - 1], tree_table[n], history_buffer, start_wid);

	phoneme = (tree_table[n]->size == 0) ? NULL : unwind_phoneme(model,
			((tree_element_t*) array_heap_element(tree_table[n], 0))->parent);

	for (i = 0; i <= n; i++) {
		for (j = 0; j < tree_table[i]->size; j++) {
			ckd_free(array_heap_element(tree_table[i], j));
		}
		array_heap_free(tree_table[i]);
	}

	ckd_free(tree_table);
	ckd_free(history_buffer);
	return phoneme;
}

int main() {
	logmath_t *logmath;
	ngram_model_t *model;
	FILE *fp;
	char *line = NULL, *grapheme, *phoneme, *predicted_phoneme;
	int32 different_word_count = 0, fit_count = 0;
	size_t len = 0;

	err_set_logfp(NULL);
	logmath = logmath_init(1.0001f, 0, 0);
	model = ngram_model_read(NULL, "cmudict-en-us.dmp",
			NGRAM_AUTO, logmath);

	fp = fopen("cmudict-en-us.dict", "r");

	while (getline(&line, &len, fp) != -1) {
		grapheme = strtok(line, " ");
		phoneme = strtok(NULL, "\n");

		if (strstr(grapheme, "(")) {
			grapheme = strtok(grapheme, "(");
		} else {
			different_word_count++;
		}

		predicted_phoneme = g2p(model, grapheme, 100);
		if (predicted_phoneme && strcmp(phoneme, predicted_phoneme) == 0) {
			fit_count++;
		}

		ckd_free(predicted_phoneme);
	}

	printf("%d %d %f\n", fit_count, different_word_count, fit_count * 1.0 / different_word_count);

	ckd_free(grapheme);
	fclose(fp);
	ngram_model_free(model);
	logmath_free(logmath);
}

