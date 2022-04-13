<h1>Bringing Linearly Transformed Cosines to Anisotropic GGX</h1>
<p style="margin-bottom: 0px;"><i><b>ACM SIGGRAPH Symposium on Interactive 3D Graphics and Games</b> (I3D), 2022</i></p>
<span>
  <a href="https://scholar.google.co.in/citations?user=itJ7vawAAAAJ&hl=en">Aakash KT<sup>1</sup></a>,
  <a target="_blank" href="https://eheitzresearch.wordpress.com/">Eric Heitz<sup>2</sup></a>,
              <a target="_blank" href="https://onrendering.com/">Jonathan Dupuy<sup>2</sup></a>,
              <a target="_blank" href="https://scholar.google.co.in/citations?user=3HKjt_IAAAAJ&hl=en">P. J. Narayanan<sup>1</sup></a>
</span>
<p><sup>1</sup>CVIT, KCIS, IIIT Hyderabad, <sup>2</sup>Unity Technologies</p>
<hr>

<h3><b>Abstract</b></h3>
<p>
Linearly Transformed Cosines (LTCs) are a family of distributions that are used for real-time area-light shading thanks to their analytic integration properties. 
Modern game engines use an LTC approximation of the ubiquitous GGX model, but currently this approximation only exists for isotropic GGX and thus anisotropic GGX is not supported. 
While the higher dimensionality presents a challenge in itself, we show that several additional problems arise when fitting, post-processing, storing, and interpolating LTCs in the anisotropic case.
Each of these operations must be done carefully to avoid rendering artifacts.
We find robust solutions for each operation by introducing and exploiting invariance properties of LTCs. 
As a result, we obtain a small $8^4$ look-up table that provides a plausible and artifact-free LTC approximation to anisotropic GGX and brings it to real-time area-light shading.
</p>
<span>
  <a target="_blank" href="https://aakashkt.github.io/ltc_anisotropic.html">[Project Page]</a>
  <a target="_blank" href="https://arxiv.org/abs/2203.11904">[Author's Version]</a>
  <a target="_blank" href="https://drive.google.com/file/d/1UmRz1AEGkShMwdG6mJZnpIeC4mfa-hrn/view?usp=sharing">[Supplementary]</a>
  <a target="_blank" href="ltc_anisotropic_bibtex.txt">[bibtex]</a>
</span>
<hr>

<h3><b>Using the Code</b></h3>
<p>Fitting your own LUT can be done by:</p>
```python
python fit_alphax_alphay_theta_phi.py --savedir [directory_to_save_lut] --epochs [optimization_iterations]
```
