import { useEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { OBJLoader } from 'three/examples/jsm/loaders/OBJLoader.js';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';

export default function Viewer({ url }: { url: string }) {
  const ref = useRef<HTMLDivElement>(null);
  const [failed, setFailed] = useState(false);

  useEffect(() => {
    const container = ref.current;
    if (!container) return;
    setFailed(false);

    const width = container.clientWidth || 420;
    const height = container.clientHeight || 320;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x14161f);

    const camera = new THREE.PerspectiveCamera(50, width / height, 1, 100000);
    camera.position.set(0, 0, 5000);

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(width, height);
    container.appendChild(renderer.domElement);

    const controls = new OrbitControls(camera, renderer.domElement);

    scene.add(new THREE.AmbientLight(0xffffff, 0.7));
    const dir = new THREE.DirectionalLight(0xffffff, 1.2);
    dir.position.set(1, 1, 1);
    scene.add(dir);

    const loader = new OBJLoader();
    loader.load(
      url,
      (obj) => {
        const box = new THREE.Box3().setFromObject(obj);
        const center = box.getCenter(new THREE.Vector3());
        const size = box.getSize(new THREE.Vector3());
        const maxDim = Math.max(size.x, size.y, size.z) || 1;
        obj.position.sub(center);
        obj.scale.setScalar(2000 / maxDim);
        scene.add(obj);
      },
      undefined,
      () => setFailed(true),
    );

    let raf = 0;
    const animate = () => {
      raf = requestAnimationFrame(animate);
      controls.update();
      renderer.render(scene, camera);
    };
    animate();

    return () => {
      cancelAnimationFrame(raf);
      controls.dispose();
      renderer.dispose();
      if (renderer.domElement.parentElement === container) {
        container.removeChild(renderer.domElement);
      }
    };
  }, [url]);

  return (
    <div className="viewer" style={{ position: 'relative' }}>
      <div ref={ref} style={{ width: '100%', height: '100%' }} />
      {failed && (
        <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#d4d4d4', fontSize: 13, padding: 16, textAlign: 'center' }}>
          3D preview unavailable (GDML converter is not installed)
        </div>
      )}
    </div>
  );
}
