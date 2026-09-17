// A save only clears the revision whose bytes were requested. Edits (including
// replacing/resetting the document) made while the host writes remain visible.
export function createSaveState() {
  let revision = 0;
  return {
    changed() { revision += 1; },
    async save({ prepare, persist, onSaved }) {
      const savedRevision = revision;
      const result = await prepare();
      const handled = await persist(result);
      if (savedRevision === revision) onSaved();
      return handled;
    },
  };
}
