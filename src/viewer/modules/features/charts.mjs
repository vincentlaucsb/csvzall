import { createChartForm, mountChartForm } from '../chart-form.mjs';
import { createDialog } from '../dialog.mjs';
import { decorateButton } from '../icons.mjs';
import { mountFeatureStyle } from './style.mjs';
export const chartsFeature = {
 id: 'charts',
 mount(shell) {
  const doc = shell.gridElement.ownerDocument;
  const removeStyle = mountFeatureStyle(doc, new URL('/assets/modules/features/charts.css', doc.baseURI));
  doc.getElementById('viewer-tools').insertAdjacentHTML('beforeend', '<button id="add-chart" type="button">Charts</button>');
  doc.body.insertAdjacentHTML('beforeend', `
  <dialog id="chart-dirty-dialog" class="csvzall-dialog" aria-labelledby="chart-dirty-title">
    <form id="chart-dirty-form" class="dialog-form">
      <h2 id="chart-dirty-title">Unsaved changes</h2>
      <p>Save or discard current changes before opening Charts.</p>
      <div class="dialog-actions">
        <button id="cancel-chart-dirty" type="button">Cancel</button>
        <button id="discard-chart-dirty" type="button">Discard changes</button>
        <button id="save-chart-dirty" type="submit">Save changes</button>
      </div>
    </form>
  </dialog>
`);
  const button = doc.getElementById('add-chart');
  decorateButton(button,'chart-bar','Charts');
  const dialog = doc.createElement('dialog'); dialog.id = 'heatmap-chart-dialog'; dialog.className = 'csvzall-dialog chart-dialog';
  dialog.setAttribute('aria-labelledby', 'chart-form-title');
  doc.body.append(dialog); createChartForm(dialog);
  const chartModal = createDialog(dialog);
  const schema = shell.schema;
  const form = mountChartForm(dialog, { schema, fetchJson: (...args) => shell.backend.fetchJson(...args), postJson: (...args) => shell.backend.postJson(...args), decorateButton, close: chartModal.close, status: shell.setStatus });
  const dirtyDialog = doc.getElementById('chart-dirty-dialog');
  const dirtyModal = createDialog(dirtyDialog);
  const dirtyForm = doc.getElementById('chart-dirty-form');
  const cancel = doc.getElementById('cancel-chart-dirty');
  const discard = doc.getElementById('discard-chart-dirty');
  dirtyForm.onsubmit = event => { event.preventDefault(); dirtyModal.close('save'); };
  cancel.onclick = () => dirtyModal.close();
  discard.onclick = () => dirtyModal.close('discard');
  let opening = false;
  let destroyed = false;
  const open = async () => {
    if (opening || chartModal.open) return; opening = true;
    try {
      await shell.finishEditing();
      if (destroyed) return;
      if (shell.dirty) {
        const action = await dirtyModal.showForResult(cancel);
        if (destroyed) return;
        if (action === 'save') await shell.save();
        else if (action === 'discard') await shell.reset();
        else return;
        if (shell.dirty) return;
      }
      if (destroyed) return;
      Object.assign(schema, shell.schema);
      chartModal.show(); await form.open();
    } catch (error) { if (!destroyed) shell.setStatus(error.message); }
    finally { opening = false; }
  };
  button.addEventListener('click',open);
  return { destroy() {
    destroyed = true;
    removeStyle();
    button.removeEventListener('click', open);
    form.dispose();
    chartModal.destroy();
    dirtyModal.destroy();
    dialog.remove();
    dirtyDialog.remove();
    button.remove();
  } };
 },
};
