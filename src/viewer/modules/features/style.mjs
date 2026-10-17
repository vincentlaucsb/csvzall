export function mountFeatureStyle(documentRef, url) {
  const link = documentRef.createElement('link');
  link.rel = 'stylesheet';
  link.href = url.href;
  documentRef.head.append(link);
  return () => link.remove();
}
