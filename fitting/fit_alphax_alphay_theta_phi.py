import argparse, os, sys, time, random, math
from tqdm import tqdm

import numpy as np
import torch

import anisotropic_ggx
import ltc
import utils

def optimize_loop(args, model, target):
    device = torch.device("cuda:0")

    alphax_list = torch.zeros((8,8,8,8), dtype=torch.float, device=device)
    alphay_list = torch.zeros((8,8,8,8), dtype=torch.float, device=device)
    theta_list = torch.zeros((8,8,8,8), dtype=torch.float, device=device)
    phi_list = torch.zeros((8,8,8,8), dtype=torch.float, device=device)

    for alphax in range(8):
        for alphay in range(8):
            for theta in range(8):
                for phi in range(8):
                    # alphax and alphay in range [0, 1]
                    ax = alphax / 7.0
                    ay = alphay / 7.0
                    ax = max(ax, 0.0001)
                    ay = max(ay, 0.0001)
                    
                    # theta and phi in range [0, pi/2]
                    t = theta / 7.0 * 0.9999 * math.pi/2.0 # Mul by 0.999 to avoid grazing angle
                    p = phi / 7.0 * math.pi/2.0

                    # store
                    alphax_list[alphax, alphay, theta, phi] = ax
                    alphay_list[alphax, alphay, theta, phi] = ay
                    theta_list[alphax, alphay, theta, phi] = t
                    phi_list[alphax, alphay, theta, phi] = p

    '''
    Generate a GGX samples buffer for each view direction (each cell) in the LUT
    '''
    with torch.no_grad():
        
        # (8*8*8*8, )
        alphax_list = alphax_list.flatten()
        alphay_list = alphay_list.flatten()
        # (8*8*8*8, 1)
        theta_list = torch.unsqueeze( theta_list.flatten(), dim=1 )
        phi_list = torch.unsqueeze( phi_list.flatten(), dim=1 )

        # View vector generated for each cell in LUT, since each cell has theta and phi fixed
        # (8*8*8*8, 3)
        wo = torch.hstack((
            torch.sin(theta_list)*torch.cos(phi_list),
            torch.sin(theta_list)*torch.sin(phi_list), 
            torch.cos(theta_list)
        ))

        pbar = tqdm(total=wo.shape[0], desc ="Generating GGX Buffer")
        
        '''
        Use rejection sampling to sample exactly the Microfacet Anisotropic GGX.
        Sample 4*omega since rejection sampling will reduce the number.
        Finally, use only 'omega' number of samples
        '''
        ggx_aniso_serial = anisotropic_ggx.GGXAniso()
        ggx_buffer = utils.rejection_sampling(4*args.omega, ggx_aniso_serial, wo[0], alphax_list[0], alphay_list[0])
        ggx_buffer = torch.unsqueeze(ggx_buffer[:args.omega, :], dim=0) # (1, omega, 3)

        # Repeat for all parameter combinations in LUT
        for i in range(1, wo.shape[0]):
            temp = utils.rejection_sampling(4*args.omega, ggx_aniso_serial, wo[i], alphax_list[i], alphay_list[i])
            temp = torch.unsqueeze(temp[:args.omega, :], dim=0)

            ggx_buffer = torch.cat((ggx_buffer, temp), dim=0)

            pbar.update(1)
        
        pbar.close()

        # ggx_buffer size is now (8*8*8*8, omega, 3)
        # It contains, for each cell, 'omega' number of samples from the corresponding GGX dist.

    opt = torch.optim.SGD(model.parameters(), lr=args.lr, momentum=0.0)
    
    # First, optimize for 3x3 matrix
    pbar = tqdm(total=args.epochs, desc ="Optimizing 3x3")

    mul_fac = int(wo.shape[0] / args.batch) # Batching

    eps = 0.1 # Weight for the SW loss. Multiple eps by 0.999 each epoch.
    model.train()
    model.optimize_mat()

    for epoch in range(args.epochs):
        
        with torch.enable_grad():
            wi_cos = utils.sample_cosine_batch(wo.shape[0], args.omega) # Cosine sample hemisphere
            ggx_buffer = utils.buffer_rejection(wo.shape[0], args.omega, ggx_buffer, target, wo, alphax_list, alphay_list) # Replace samples from buffer using rejection sampling

            ltc_wi = model.transform_to_target(wi_cos) # Transform all directions to target space using matrices from the current LUT

            # Backpropagate in batches
            for idx in range(0, args.batch):
                opt.zero_grad()

                NUMBER_OF_DIRECTIONS = 64
                directions = torch.randn(wo[idx*mul_fac:(idx+1)*mul_fac].shape[0], NUMBER_OF_DIRECTIONS, 3, device=device)
                directions = directions / torch.norm(directions, dim=2, keepdim=True)

                # Project transformed and GGX directions to random directions
                feat_op_projected = torch.einsum('abc,aec->aeb', ltc_wi[idx*mul_fac:(idx+1)*mul_fac], directions)
                feat_gt_projected = torch.einsum('abc,aec->aeb', ggx_buffer[idx*mul_fac:(idx+1)*mul_fac], directions)

                # Sort both projections
                feat_op_projected = torch.sort(feat_op_projected, dim=2)[0]
                feat_gt_projected = torch.sort(feat_gt_projected, dim=2)[0]

                # Take L1 loss on the sorted projections
                loss = (feat_gt_projected - feat_op_projected).abs()
                loss = torch.mean(torch.mean(loss, dim=2), dim=1)
                loss = eps * torch.sum(loss)

                loss.backward(retain_graph=True)
                opt.step()

            eps *= 0.999

            pbar.update(1)
        
    pbar.close()    
    torch.cuda.empty_cache()
    
    # Alignment step: Kill random rotation and flipping to get correct interpolation of LTC matrices
    with torch.no_grad():
        model.freeze()

        cosine_samples = utils.sample_cosine(10000)

        # Define four kind of possible flips in the X-Y plane
        flip1 = torch.tensor([
            [-1, 0, 0],
            [0, -1, 0],
            [0, 0, 1]
        ], dtype=torch.float, device=device)

        flip2 = torch.tensor([
            [1, 0, 0],
            [0, -1, 0],
            [0, 0, 1]
        ], dtype=torch.float, device=device)

        flip3 = torch.tensor([
            [-1, 0, 0],
            [0, 1, 0],
            [0, 0, 1]
        ], dtype=torch.float, device=device)

        flip4 = torch.tensor([
            [1, 0, 0],
            [0, 1, 0],
            [0, 0, 1]
        ], dtype=torch.float, device=device)

        pbar = tqdm(total=model.lut_size[0]*model.lut_size[1]*model.lut_size[2]*model.lut_size[3], desc ="Killing Rotation")

        eps_div = []
        for i in range(args.eps_divs):
            eps_div.append( 10**(i+1) )

        # Linear search for the rotation angle in the X-Y plane
        for alphax_idx in range(model.lut_size[0]):
            for alphay_idx in range(model.lut_size[1]):
                for theta_idx in range(model.lut_size[2]):
                    for phi_idx in range(model.lut_size[3]):
                
                        current_ltc = model.get_mat(alphax_idx, alphay_idx, theta_idx, phi_idx)

                        losses = []
                        MRF_mats = []
                        for c in range(4):
                            if c == 0:
                                flip = flip1
                            elif c == 1:
                                flip = flip2
                            elif c == 2:
                                flip = flip3
                            elif c == 3:
                                flip = flip4

                            eps_step = 0.1

                            lb = 0.0
                            ub = 2*np.pi

                            loss = 1000000
                            best_MRF = None
                            best_phi = None

                            for div in eps_div:

                                phi = torch.arange(lb, ub, eps_step, dtype=torch.float, device=device)
                                for idx, item in enumerate(phi):
                                    R = torch.tensor([
                                        [torch.cos(item), -torch.sin(item), 0.0],
                                        [torch.sin(item), torch.cos(item), 0.0],
                                        [0.0, 0.0, 1.0]
                                    ], dtype=torch.float, device=device)
                                    
                                    MRF = current_ltc @ R @ flip

                                    ltc_vec = (MRF @ cosine_samples.T).T
                                    ltc_vec = ltc_vec / torch.linalg.norm(ltc_vec, dim=1, keepdim=True)

                                    loss_ = torch.mean((cosine_samples-ltc_vec)**2)

                                    if loss_ < loss:
                                        loss = loss_
                                        best_MRF = MRF
                                        best_phi = item
                                
                                eps_step /= div
                                lb = best_phi - eps_step
                                ub = best_phi + eps_step
                            
                            losses.append(loss.cpu().numpy())
                            MRF_mats.append(best_MRF)
                        
                        losses = np.array(losses)
                        min_loss_idx = np.argmin(losses)

                        model.LUT.data[alphax_idx, alphay_idx, theta_idx, phi_idx, :, :] = MRF_mats[ min_loss_idx ]

                        pbar.update(1)

        pbar.close()
    
    # Get the final LUT
    ltc_mat = model.LUT.detach().cpu().numpy()
    
    ###################################################
    # Force coefficients to zero for particular configs
    ###################################################

    # First, for phi=0 (or phi=pi), 
    # M = [a 0 b
    #      0 c 0
    #      d 0 f]
    ltc_mat[:, :, :, 0, 0, 1] = 0
    ltc_mat[:, :, :, 0, 1, 0] = 0
    ltc_mat[:, :, :, 0, 1, 2] = 0
    ltc_mat[:, :, :, 0, 2, 1] = 0 

    # First, for phi=pi/2 (or phi=3*pi/2), 
    # M = [a 0 0
    #      0 b c
    #      0 d e]
    ltc_mat[:, :, :, 7, 0, 1] = 0
    ltc_mat[:, :, :, 7, 0, 2] = 0
    ltc_mat[:, :, :, 7, 1, 0] = 0
    ltc_mat[:, :, :, 7, 2, 0] = 0

    # For theta=0, set the following matrix
    # M = [a 0 0
    #      0 b 0
    #      0 0 c]
    ltc_mat[:, :, 0, :, 0, 1] = 0
    ltc_mat[:, :, 0, :, 0, 2] = 0
    ltc_mat[:, :, 0, :, 1, 0] = 0
    ltc_mat[:, :, 0, :, 1, 2] = 0
    ltc_mat[:, :, 0, :, 2, 0] = 0
    ltc_mat[:, :, 0, :, 2, 1] = 0

    # Again, for theta=0, set the same matrix for all phis
    for i in range(0, lut_size[3]):
        ltc_mat[:, :, 0, i] = ltc_mat[:, :, 0, 0]

    # Normalize matrix, by dividing by norm of last row
    # This works due to scale invariance of LTC!
    ltc_mat[:, :, :, :, :, :] = ltc_mat[:, :, :, :, :, :] / np.linalg.norm(ltc_mat[:, :, :, :, :, 2:3], axis=-2, keepdims=True)
    
    np.save('%s/alphax_alphay_theta_phi.npy' % (args.savedir), ltc_mat)



if __name__ == '__main__':

    parser = argparse.ArgumentParser(description='')
    parser.add_argument('--epochs', type=int, default=10000) # Number of optimization iterations
    parser.add_argument('--savedir', type=str, help='') # Where to save the LUT
    # Bins for each hyperparameter
    parser.add_argument('--alphax_bins', type=int, default=8)
    parser.add_argument('--alphay_bins', type=int, default=8)
    parser.add_argument('--theta_bins', type=int, default=8)
    parser.add_argument('--phi_bins', type=int, default=8)
    parser.add_argument('--batch', type=int, default=8) # Divide optimization into batches, reduces memory on GPU
    parser.add_argument('--omega', type=int, default=2048) # Number of samples for taking loss (LTC - BRDF)
    parser.add_argument('--lr', type=float, default=1.0) # Learning rate for SGD optimizer, 1.0 works well
    '''
    Used only in the alignment stage.
    For 1D linear search of the value 'phi' in alignment.
    Progresively use finer division between lower and upper phi values.
    Number of progressive steps is defined by eps_divs. (See line ?? for more details)
    '''
    parser.add_argument('--eps_divs', type=int, default=3)

    args = parser.parse_args()

    lut_size = (args.alphax_bins, args.alphay_bins, args.theta_bins, args.phi_bins)

    savedir = os.path.join(args.savedir)
    if not os.path.exists(savedir):
        os.makedirs(savedir)
    
    target = anisotropic_ggx.GGXAnisoBatch()

    model = ltc.LTCAnisotropic(lut_size=lut_size)
    model.cuda()

    optimize_loop(args, model, target)