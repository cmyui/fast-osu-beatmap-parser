"""Corpus selection shared by Python benchmark drivers."""

def select_files(parser, corpus, reps, limit=0):
    files = sorted(corpus.glob('*.osu'))
    if not files or reps < 1 or limit < 0:
        parser.error('nonempty corpus, positive reps and nonnegative limit required')
    if 0 < limit < len(files):
        files = [files[i * len(files) // limit] for i in range(limit)]
    return files
